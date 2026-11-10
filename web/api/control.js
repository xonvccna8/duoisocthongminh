// POST /api/control  cmd, v, pin – lệnh điều khiển từ xa, xếp hàng chờ thiết bị lấy ở lần gửi kế tiếp.
// Cần mã PIN (đặt trên web của thiết bị, mục Internet). Sai 10 lần thì khóa 10 phút.
const { kv, readForm, sha, noCache, fail } = require('./_kho');

const ALLOW = ['arm', 'auto', 'repel', 'led', 'mist', 'sound', 'stopsound', 'volume', 'photo', 'stop'];

module.exports = async (req, res) => {
  noCache(res);
  if (req.method !== 'POST') return res.status(405).json({ ok: false });
  try {
    const f = await readForm(req);
    if (!ALLOW.includes(f.cmd)) return res.status(400).json({ ok: false, err: 'Lệnh không hợp lệ' });
    const [pinh, fails] = await kv(['GET', 'ds:pin'], ['GET', 'ds:pinfail']);
    if (!pinh) return res.status(403).json({ ok: false, err: 'Thiết bị chưa đặt mã PIN điều khiển từ xa' });
    if (+fails >= 10) return res.status(429).json({ ok: false, err: 'Sai PIN quá nhiều, thử lại sau 10 phút' });
    if (sha(f.pin || '') !== pinh) {
      await kv(['INCR', 'ds:pinfail'], ['EXPIRE', 'ds:pinfail', '600']);
      return res.status(403).json({ ok: false, err: 'Sai mã PIN', pin: true });
    }
    const c = JSON.stringify({ c: f.cmd, v: parseInt(f.v, 10) || 0, at: Math.floor(Date.now() / 1000) });
    await kv(['RPUSH', 'ds:cmdq', c], ['EXPIRE', 'ds:cmdq', '300'], ['SET', 'ds:viewer', '1', 'EX', '30']);
    res.status(200).json({ ok: true, queued: true });
  } catch (e) { fail(res, e); }
};
