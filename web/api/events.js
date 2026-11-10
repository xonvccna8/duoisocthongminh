// GET /api/events – nhật ký sự kiện thiết bị gửi lên gần nhất
const { kv, noCache, fail } = require('./_kho');

module.exports = async (req, res) => {
  noCache(res);
  try {
    const [raw] = await kv(['GET', 'ds:status']);
    res.status(200).json(raw ? JSON.parse(raw).events || [] : []);
  } catch (e) { fail(res, e); }
};
