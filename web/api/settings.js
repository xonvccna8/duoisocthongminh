// GET /api/settings – cài đặt hiện tại của thiết bị (chỉ xem). Đổi cài đặt phải làm trên web của thiết bị.
const { kv, noCache, fail } = require('./_kho');

module.exports = async (req, res) => {
  noCache(res);
  if (req.method === 'POST') return res.status(403).json({ ok: false, err: 'Đổi cài đặt trên web của thiết bị (trong vườn)' });
  try {
    const [raw] = await kv(['GET', 'ds:status']);
    res.status(200).json(raw ? (JSON.parse(raw).status || {}).cfg || {} : {});
  } catch (e) { fail(res, e); }
};
