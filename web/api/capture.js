// GET /capture (vercel.json chuyển về đây) – ảnh camera mới nhất (thiết bị gửi liên tục ~2 ảnh/giây khi có người xem)
const { kv, noCache, fail } = require('./_kho');

module.exports = async (req, res) => {
  noCache(res);
  try {
    const [b64] = await kv(['GET', 'ds:live'], ['SET', 'ds:viewer', '1', 'EX', '30']);
    if (!b64) return res.status(404).send('Chua co anh');
    res.setHeader('Content-Type', 'image/jpeg');
    res.status(200).send(Buffer.from(b64, 'base64'));
  } catch (e) { fail(res, e); }
};
