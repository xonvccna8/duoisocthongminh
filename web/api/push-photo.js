// POST /api/push-photo?id=N   (ảnh sóc trên thẻ nhớ, giữ 7 ngày)
// POST /api/push-photo?live=1 (ảnh camera trực tiếp khi có người xem, giữ 2 phút)
// header X-Device-Key, thân là ảnh JPEG.
const { kv, readBody, sha, noCache, fail } = require('./_kho');

const LUA = `
if redis.call('GET', KEYS[1]) ~= ARGV[1] then return 0 end
redis.call('SET', KEYS[2], ARGV[2], 'EX', ARGV[3])
return 1`;

module.exports = async (req, res) => {
  noCache(res);
  if (req.method !== 'POST') return res.status(405).json({ ok: false });
  const key = String(req.headers['x-device-key'] || '');
  const live = req.query.live === '1';
  const id = parseInt(req.query.id, 10);
  if (key.length < 16 || (!live && !(id >= 0))) return res.status(400).json({ ok: false });
  try {
    const jpg = await readBody(req);
    if (jpg.length < 100 || jpg[0] !== 0xff || jpg[1] !== 0xd8) return res.status(400).json({ ok: false, err: 'khong phai JPEG' });
    const [ok] = await kv(['EVAL', LUA, '2', 'ds:devkey', live ? 'ds:live' : 'ds:photo:' + id,
      sha(key), jpg.toString('base64'), live ? '120' : String(7 * 86400)]);
    if (!ok) return res.status(403).json({ ok: false, err: 'sai khoa thiet bi' });
    res.status(200).json({ ok: true });
  } catch (e) { fail(res, e); }
};
