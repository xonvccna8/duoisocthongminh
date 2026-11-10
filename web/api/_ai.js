// AI nhận diện sóc chạy trên Vercel: giống hệt AI trong trang web (MobileNet v2, quét 26 vùng ảnh),
// dùng TensorFlow.js + WebAssembly. Mô hình nạp một lần rồi giữ lại cho các lần gọi sau.
const path = require('path');
const tf = require('@tensorflow/tfjs');
const wasm = require('@tensorflow/tfjs-backend-wasm');
const mobilenet = require('@tensorflow-models/mobilenet');
const jpeg = require('jpeg-js');

const SQ = 335;   // lớp "fox squirrel" trong ImageNet
// Vùng quét [x, y, rộng, cao] theo tỉ lệ ảnh: cả khung + lưới 4x4 (40%) + lưới 3x3 (60%)
const BOXES = (() => {
  const b = [[0, 0, 1, 1]];
  for (const [s, n] of [[0.4, 4], [0.6, 3]]) for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) b.push([x * 0.2, y * 0.2, s, s]);
  return b;
})();

let ready = null;
function load() {
  if (!ready) {
    ready = (async () => {
      wasm.setWasmPaths(path.join(path.dirname(require.resolve('@tensorflow/tfjs-backend-wasm')), path.sep));
      if (!(await tf.setBackend('wasm').catch(() => false))) await tf.setBackend('cpu');
      await tf.ready();
      return mobilenet.load({ version: 2, alpha: 1 });
    })().catch(e => { ready = null; throw e; });
  }
  return ready;
}

// Ảnh JPEG -> {score 0..100, p xác suất sóc, box [x,y,w,h] theo tỉ lệ hoặc null, ms, backend}
async function analyze(buf) {
  const model = await load();
  const t0 = Date.now();
  const { width, height, data } = jpeg.decode(buf, { useTArray: true, formatAsRGBA: false, maxMemoryUsageInMB: 256 });
  const ps = tf.tidy(() => {
    const px = tf.tensor3d(data, [height, width, 3], 'int32').toFloat().expandDims(0);
    const crops = tf.image.cropAndResize(px, tf.tensor2d(BOXES.map(([x, y, w, h]) => [y, x, y + h, x + w])),
      tf.zeros([BOXES.length], 'int32'), [224, 224]);
    return tf.softmax(model.infer(crops)).slice([0, SQ], [-1, 1]);
  });
  const p = [...(await ps.data())];
  ps.dispose();
  let best = 0;
  p.forEach((q, i) => { if (q > p[best]) best = i; });
  let box = null;
  if (p[best] >= 0.1) {   // vị trí sóc: trung bình có trọng số các vùng >= 40% vùng cao nhất
    let sw = 0;
    box = [0, 0, 0, 0];
    p.forEach((q, i) => { if (q >= p[best] * 0.4) { sw += q; BOXES[i].forEach((v, k) => { box[k] += v * q; }); } });
    box = box.map(v => +(v / sw).toFixed(3));
  }
  // MobileNet chỉ có lớp sóc Bắc Mỹ nên với sóc Việt Nam xác suất thường 0,4–0,8: căn bậc hai đưa về thang 0–100
  return { score: Math.round(Math.sqrt(p[best]) * 100), p: +p[best].toFixed(4), box, ms: Date.now() - t0, backend: tf.getBackend(), w: width, h: height };
}

module.exports = { analyze, load };
