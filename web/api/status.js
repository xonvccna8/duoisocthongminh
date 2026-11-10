// GET /api/status – giống API của thiết bị, nhưng đọc bản thiết bị gửi lên gần nhất (xem từ xa).
// Đánh dấu "có người đang xem" 30 giây để thiết bị gửi dày hơn (5 giây/lần).
// rejectAgo: có một board đang gửi lên nhưng bị từ chối vì khác khóa thiết bị (thay board / nạp lại xóa flash),
// cách đây bao nhiêu giây; rejectPin: board đó đã đặt PIN chưa (đặt rồi mà vẫn bị từ chối = sai PIN cũ).
const { kv, noCache, fail } = require('./_kho');

function rejectInfo(rej) {
  if (!rej) return {};
  const [at, hasPin] = String(rej).split('|');
  return { rejectAgo: Math.max(0, Math.floor(Date.now() / 1000) - (+at || 0)), rejectPin: hasPin === '1' };
}

module.exports = async (req, res) => {
  noCache(res);
  try {
    const [raw, , rej] = await kv(['GET', 'ds:status'], ['SET', 'ds:viewer', '1', 'EX', '30'], ['GET', 'ds:reject']);
    if (!raw) return res.status(404).json({ ok: false, err: 'Chưa có thiết bị gửi dữ liệu lên', ...rejectInfo(rej) });
    const d = JSON.parse(raw);
    const st = d.status || {};
    st.remote = true;
    st.age = Math.max(0, Math.floor(Date.now() / 1000) - d.pushedAt);
    if (st.ts) st.ts += st.age;
    Object.assign(st, rejectInfo(rej));
    res.status(200).json(st);
  } catch (e) { fail(res, e); }
};
