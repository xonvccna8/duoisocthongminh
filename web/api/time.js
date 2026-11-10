// POST /api/time – khi xem từ xa, thiết bị tự lấy giờ qua Internet nên không cần đồng bộ từ trình duyệt
module.exports = (req, res) => res.status(200).json({ ok: true, remote: true });
