// POST /api/push  (thiết bị gửi lên)  header X-Device-Key, thân JSON {status, events, pinHash}
// Trả {ok, fast, cmds}: fast = có người đang xem web -> thiết bị gửi dày hơn; cmds = lệnh điều khiển từ xa.
// Khóa thiết bị: thiết bị gửi lần đầu được ghi nhận (lưu mã băm), sau đó chỉ đúng khóa đó mới gửi được.
// Thay board / nạp lại xóa sạch flash -> board tự sinh khóa mới và bị từ chối (403). Khi đó chỉ cần nhập lại
// ĐÚNG mã PIN cũ trên web thiết bị (mục Internet): mã PIN khớp thì máy chủ nhận khóa mới, không phải vào kho Upstash.
// Thử PIN sai dùng chung bộ đếm "sai 10 lần khóa 10 phút" với /api/control (chỉ đếm khi đổi sang PIN khác,
// board gửi đi gửi lại cùng một PIN sai không làm khóa nút điều khiển của chủ vườn).
// Mỗi lần từ chối được ghi lại (ds:reject) để web báo đúng lý do thay vì chỉ "offline".
const { kv, readBody, sha, noCache, fail } = require('./_kho');

const LUA = `
redis.call('SET', KEYS[1], ARGV[1], 'NX')
if redis.call('GET', KEYS[1]) ~= ARGV[1] then
  local p = redis.call('GET', KEYS[5])
  local f = tonumber(redis.call('GET', KEYS[6]) or '0') or 0
  if ARGV[3] ~= '' and p and f < 10 and p == ARGV[3] then
    redis.call('SET', KEYS[1], ARGV[1])
    redis.call('DEL', KEYS[7])
  else
    if ARGV[3] ~= '' and p and redis.call('GET', KEYS[8]) ~= ARGV[3] then
      redis.call('SET', KEYS[8], ARGV[3], 'EX', 600)
      redis.call('INCR', KEYS[6])
      redis.call('EXPIRE', KEYS[6], 600)
    end
    redis.call('SET', KEYS[7], ARGV[4] .. '|' .. (ARGV[3] ~= '' and '1' or '0'), 'EX', 604800)
    return {0}
  end
end
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
    const now = Math.floor(Date.now() / 1000);
    const [r] = await kv(['EVAL', LUA, '8', 'ds:devkey', 'ds:status', 'ds:cmdq', 'ds:viewer', 'ds:pin', 'ds:pinfail', 'ds:reject',
      'ds:rebindtry', sha(key), data, pin, String(now)]);
    if (!r[0]) return res.status(403).json({ ok: false, err: 'sai khoa thiet bi: nhap lai dung ma PIN cu tren web thiet bi' });
    const cmds = r.slice(2).map(s => { try { return JSON.parse(s); } catch { return null; } })
      .filter(c => c && now - c.at < 120);
    res.status(200).json({ ok: true, fast: r[1] === 1, cmds });
  } catch (e) { fail(res, e); }
};
