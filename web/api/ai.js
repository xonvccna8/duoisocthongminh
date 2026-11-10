// POST /api/ai[?keep=1]  (thiết bị gửi ảnh camera lúc PIR báo chuyển động)  header X-Device-Key, thân là ảnh JPEG
// Trả {ok, score 0..100, p, box, ms, backend, photo}: AI chấm "có sóc không"; thiết bị đủ điểm thì xua đuổi.
// AI chính: YOLO26n tự huấn luyện nhận sóc (_yolo.js, models/soc_yolo26n.onnx). Chưa có mô hình hoặc YOLO lỗi
// thì dùng MobileNet (_ai.js) như cũ.
// keep=1 (thiết bị không có thẻ nhớ): lưu luôn ảnh 7 ngày, trả số ảnh để nhật ký trên web hiện được ảnh.
const { kv, readBody, sha, noCache, fail } = require('./_kho');
const yolo = require('./_yolo');

async function analyze(jpg) {
  if (yolo.available()) {
    try { return await yolo.analyze(jpg); } catch (e) { console.error('YOLO loi, dung MobileNet:', e.message); }
  }
  return require('./_ai').analyze(jpg);   // nạp TensorFlow.js chỉ khi cần
}

const LUA_KEY = `
redis.call('SET', KEYS[1], ARGV[1], 'NX')
if redis.call('GET', KEYS[1]) ~= ARGV[1] then return 0 end
return 1`;
const LUA_KEEP = `
local id = 50000 + (redis.call('INCR', KEYS[1]) % 10000)
redis.call('SET', 'ds:photo:' .. id, ARGV[1], 'EX', ARGV[2])
return id`;

module.exports = async (req, res) => {
  noCache(res);
  if (req.method !== 'POST') return res.status(405).json({ ok: false });
  const key = String(req.headers['x-device-key'] || '');
  if (key.length < 16) return res.status(401).json({ ok: false, err: 'thieu khoa thiet bi' });
  try {
    const jpg = await readBody(req);
    if (jpg.length < 100 || jpg[0] !== 0xff || jpg[1] !== 0xd8) return res.status(400).json({ ok: false, err: 'khong phai JPEG' });
    const [okKey] = await kv(['EVAL', LUA_KEY, '1', 'ds:devkey', sha(key)]);
    if (!okKey) return res.status(403).json({ ok: false, err: 'sai khoa thiet bi' });
    const r = await analyze(jpg);
    let photo = -1;
    if (req.query.keep === '1') [photo] = await kv(['EVAL', LUA_KEEP, '1', 'ds:aiseq', jpg.toString('base64'), String(7 * 86400)]);
    res.status(200).json({ ok: true, ...r, photo });
  } catch (e) { fail(res, e); }
};
