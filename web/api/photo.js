// GET /photo?id=N (vercel.json chuyển về đây) – ảnh sóc thiết bị đã gửi lên
const { kv, fail } = require('./_kho');

module.exports = async (req, res) => {
  const id = parseInt(req.query.id, 10);
  if (!(id >= 0)) return res.status(400).end();
  try {
    const [b64] = await kv(['GET', 'ds:photo:' + id]);
    if (!b64) return res.status(404).send('Khong co anh');
    res.setHeader('Content-Type', 'image/jpeg');
    res.setHeader('Cache-Control', 'public, max-age=86400, immutable');
    res.status(200).send(Buffer.from(b64, 'base64'));
  } catch (e) { fail(res, e); }
};
