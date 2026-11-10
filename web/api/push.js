// POST /api/push  (thiết bị gửi lên)  header X-Device-Key, thân JSON {status, events, pinHash}
// Trả {ok, fast, cmds}: fast = có người đang xem web -> thiết bị gửi dày hơn; cmds = lệnh điều khiển từ xa.
// Khóa thiết bị: thiết bị gửi lần đầu được ghi nhận (lưu mã băm), sau đó chỉ đúng khóa đó mới gửi được.
const { kv, readBody, sha, noCache, fail } = require('./_kho');

const LUA = `
redis.call('SET', KEYS[1], ARGV[1], 'NX')
if redis.call('GET', KEYS[1]) ~= ARGV[1] then return {0} end
redis.call('SET', KEYS[2], ARGV[2])
if ARGV[3] ~= '' then redis.call('SET', KEYS[5], ARGV[3]) end
local c = redis.call('LRANGE', KEYS[3], 0, -1)
redis.call('DEL', KEYS[3])
return {1, redis.call('EXISTS', KEYS[4]), unpack(c)}`;

module.exports = async (req, res) => {
  noCache(res);
  if (req.method !== 'POST') return res.status(405).json({ ok: false });
  const key = String(req.headers['x-device-key'] || '');
  if (key.length < 16) return res.status(401).json({ ok: false, err: 'thieu khoa thiet bi' });
  let body;
  try { body = JSON.parse((await readBody(req)).toString('utf8')); }
  catch { return res.status(400).json({ ok: false, err: 'json' }); }
  try {
    const pin = /^[0-9a-f]{64}$/.test(body.pinHash || '') ? body.pinHash : '';
    const data = JSON.stringify({ status: body.status || {}, events: body.events || [], pushedAt: Math.floor(Date.now() / 1000) });
    const [r] = await kv(['EVAL', LUA, '5', 'ds:devkey', 'ds:status', 'ds:cmdq', 'ds:viewer', 'ds:pin', sha(key), data, pin]);
    if (!r[0]) return res.status(403).json({ ok: false, err: 'sai khoa thiet bi' });
    const now = Date.now() / 1000;
    const cmds = r.slice(2).map(s => { try { return JSON.parse(s); } catch { return null; } })
      .filter(c => c && now - c.at < 120);
    res.status(200).json({ ok: true, fast: r[1] === 1, cmds });
  } catch (e) { fail(res, e); }
};
