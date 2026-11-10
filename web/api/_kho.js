// Dùng chung cho các API: kho Upstash Redis (Vercel Marketplace) qua REST, đọc thân request, băm SHA-256.
// File bắt đầu bằng "_" nên Vercel không coi là một API riêng.
const crypto = require('crypto');

const URL_ = process.env.KV_REST_API_URL || process.env.UPSTASH_REDIS_REST_URL;
const TOKEN = process.env.KV_REST_API_TOKEN || process.env.UPSTASH_REDIS_REST_TOKEN;

// kv(['GET','k'], ['SET','k','v']) -> [kết quả lệnh 1, kết quả lệnh 2]
async function kv(...cmds) {
  if (!URL_ || !TOKEN) throw new Error('Chưa nối kho Upstash Redis');
  const r = await fetch(URL_ + '/pipeline', {
    method: 'POST',
    headers: { Authorization: 'Bearer ' + TOKEN, 'Content-Type': 'application/json' },
    body: JSON.stringify(cmds),
  });
  if (!r.ok) throw new Error('Kho trả lỗi ' + r.status);
  return (await r.json()).map(x => {
    if (x.error) throw new Error(x.error);
    return x.result;
  });
}

async function readBody(req) {
  const parts = [];
  for await (const c of req) parts.push(c);
  return Buffer.concat(parts);
}

// Form (x-www-form-urlencoded) như thiết bị: cmd=...&v=...
async function readForm(req) {
  if (req.body && typeof req.body === 'object') return req.body;
  return Object.fromEntries(new URLSearchParams((await readBody(req)).toString('utf8')));
}

const sha = s => crypto.createHash('sha256').update(String(s)).digest('hex');

function noCache(res) {
  res.setHeader('Cache-Control', 'no-store');
  res.setHeader('Access-Control-Allow-Origin', '*');
}

// Báo lỗi gọn cho trang web, không lộ chi tiết kho
function fail(res, e) {
  console.error(e);
  res.status(500).json({ ok: false, err: String(e.message || e) });
}

module.exports = { kv, readBody, readForm, sha, noCache, fail };
