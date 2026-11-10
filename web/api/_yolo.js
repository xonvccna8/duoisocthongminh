// AI YOLO26n nhận diện sóc (tự huấn luyện, ai_soc/) chạy trên Vercel bằng ONNX Runtime WebAssembly.
// Mô hình models/soc_yolo26n.onnx: vào ảnh 640x640, ra 300 khung [x0, y0, x1, y1, điểm, lớp] (không cần NMS).
// Mô hình nạp một lần rồi giữ lại cho các lần gọi sau.
const fs = require('fs');
const path = require('path');
const ort = require('onnxruntime-web');
const jpeg = require('jpeg-js');

const MODEL = process.env.SOC_MODEL || path.join(process.cwd(), 'models', 'soc_yolo26n.onnx');
const SIZE = 640;
const CONF = 0.05;   // khung có độ tin cậy thấp hơn thì bỏ
// Độ tin cậy YOLO -> điểm 0..100 của thiết bị (ngưỡng mặc định 60). Đo trên 335 ảnh val/test chưa học:
// tin cậy 0,15 bắt đúng 95% ảnh sóc, báo nhầm 9,5% ảnh khó (chim, chuột, mèo trên cây) -> đặt 0,15 = 60 điểm.
const AT60 = 0.15;
const toScore = p => Math.round(p <= AT60 ? p / AT60 * 60 : 60 + (p - AT60) / (1 - AT60) * 40);

let ready = null;
function load() {
  if (!ready) {
    ready = (async () => {
      ort.env.wasm.numThreads = 1;   // máy chủ Vercel: 1 luồng, không cần worker
      // file .wasm/.mjs: ort tự tìm cạnh file ort.node.min.js (vercel.json đã kèm theo)
      return ort.InferenceSession.create(fs.readFileSync(MODEL), { executionProviders: ['wasm'] });
    })().catch(e => { ready = null; throw e; });
  }
  return ready;
}

// Thu ảnh RGB về khung 640x640 giữ tỉ lệ, phần thừa tô xám 114 (giống ultralytics), ra tensor 0..1 dạng CHW
function letterbox(data, w, h) {
  const r = Math.min(SIZE / w, SIZE / h);
  const nw = Math.round(w * r), nh = Math.round(h * r);
  const px = Math.floor((SIZE - nw) / 2), py = Math.floor((SIZE - nh) / 2);
  const out = new Float32Array(3 * SIZE * SIZE).fill(114 / 255);
  const plane = SIZE * SIZE;
  for (let y = 0; y < nh; y++) {
    const sy = Math.min(h - 1, (y + 0.5) / r - 0.5), y0 = Math.max(0, Math.floor(sy)), y1 = Math.min(h - 1, y0 + 1), fy = Math.max(0, sy - y0);
    for (let x = 0; x < nw; x++) {
      const sx = Math.min(w - 1, (x + 0.5) / r - 0.5), x0 = Math.max(0, Math.floor(sx)), x1 = Math.min(w - 1, x0 + 1), fx = Math.max(0, sx - x0);
      const o = (y + py) * SIZE + x + px;
      for (let c = 0; c < 3; c++) {
        const a = data[(y0 * w + x0) * 3 + c], b = data[(y0 * w + x1) * 3 + c];
        const d = data[(y1 * w + x0) * 3 + c], e = data[(y1 * w + x1) * 3 + c];
        out[c * plane + o] = ((a + (b - a) * fx) * (1 - fy) + (d + (e - d) * fx) * fy) / 255;
      }
    }
  }
  return { tensor: new ort.Tensor('float32', out, [1, 3, SIZE, SIZE]), r, px, py };
}

// Ảnh JPEG -> {score 0..100, p điểm khung sóc tốt nhất, box [x,y,w,h] theo tỉ lệ hoặc null, boxes, ms, backend}
async function analyze(buf) {
  const session = await load();
  const t0 = Date.now();
  const { width, height, data } = jpeg.decode(buf, { useTArray: true, formatAsRGBA: false, maxMemoryUsageInMB: 256 });
  const { tensor, r, px, py } = letterbox(data, width, height);
  const res = await session.run({ [session.inputNames[0]]: tensor });
  const det = res[session.outputNames[0]].data;   // 300 x 6
  const boxes = [];
  for (let i = 0; i < det.length; i += 6) {
    if (det[i + 4] < CONF) continue;
    const x0 = Math.max(0, (det[i] - px) / r), y0 = Math.max(0, (det[i + 1] - py) / r);
    const x1 = Math.min(width, (det[i + 2] - px) / r), y1 = Math.min(height, (det[i + 3] - py) / r);
    boxes.push({ p: +det[i + 4].toFixed(4), box: [x0 / width, y0 / height, (x1 - x0) / width, (y1 - y0) / height].map(v => +v.toFixed(3)) });
  }
  boxes.sort((a, b) => b.p - a.p);
  const best = boxes[0];
  return { score: best ? toScore(best.p) : 0, p: best ? best.p : 0, box: best ? best.box : null,
           boxes: boxes.slice(0, 5), ms: Date.now() - t0, backend: 'yolo26n-wasm', w: width, h: height };
}

module.exports = { analyze, load, available: () => fs.existsSync(MODEL) };
