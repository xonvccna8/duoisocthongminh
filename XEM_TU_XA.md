# Xem và điều khiển máy đuổi sóc từ xa qua Internet

```text
ESP32-S3 CAM (trong vườn, vào WiFi nhà có Internet)
   │  60 giây/lần gửi trạng thái + nhật ký; có sóc thì gửi ngay kèm ẢNH
   │  có người đang xem web: 5 giây/lần + ảnh camera trực tiếp
   ▼
Web Vercel  https://duoi-soc-thong-minh.vercel.app   (API trong web/api, kho Upstash Redis miễn phí)
   ▲
   │  mở ở bất cứ đâu (4G, WiFi khác...)
Điện thoại / máy tính: xem trạng thái, sự kiện, ảnh sóc; bấm Xua đuổi / Phun / Dừng (cần mã PIN)
```

Không có Internet thì máy vẫn chạy đủ chức năng trong vườn, web vẫn mở được ở `http://192.168.4.1` (WiFi `DuoiSoc`).

## AI nhận diện sóc bằng camera ESP32

```text
PIR báo chuyển động → ESP32 chụp ảnh → POST /api/ai (Vercel) → AI YOLO26n tự huấn luyện nhận sóc
   → điểm 0–100 (ảnh sóc 95–98, cây/trái không sóc 0–21) → board: điểm ≥ ngưỡng (mặc định 60) thì xua đuổi
```

- AI là mô hình **YOLO26n tự huấn luyện** trên 1673 ảnh sóc và ảnh nền: bắt đúng 95% ảnh có sóc (chi tiết: `ai_soc/README.md`). Chạy bằng ONNX Runtime WebAssembly (`api/_yolo.js`, mô hình `web/models/soc_yolo26n.onnx`); thiếu mô hình thì tự dùng MobileNet cũ.
- Board tự làm, **không cần ai mở web**. Mỗi lần chấm mất khoảng 1,5–2 giây (lần đầu sau lúc nghỉ khoảng 4 giây).
- Nhật ký ghi **"AI xác nhận có sóc"** hoặc **"Chuyển động - AI: không phải sóc"**, kèm ảnh thật.
- Board không có thẻ nhớ thì máy chủ giữ ảnh 7 ngày (số ảnh từ 50000), web từ xa vẫn xem được.
- **Chỉ xua đuổi khi có đủ 2 xác nhận**: cảm biến PIR thấy chuyển động **và** AI YOLO thấy sóc trong ảnh camera chụp ngay lúc đó. Nhật ký ghi "PIR + AI camera xác nhận có sóc".
- **Mất Internet hoặc camera lỗi** thì board **không tự xua** (không đoán mò), chỉ ghi "Chuyển động - chưa hỏi được AI". Nút **Xua đuổi ngay** trên web vẫn dùng được.
- Web **không còn dữ liệu demo**: thiết bị chưa gửi lên thì trang chỉ báo "Chờ thiết bị", mọi con số đều là số liệu thật.

## Cài đặt (một lần)

1. **Nạp firmware** bằng `NAP_FIRMWARE.bat` (bấm đúp). Firmware có phần Internet nặng hơn 1,25MB nên **không nạp bằng Arduino IDE với Partition mặc định** được; file .bat đã chọn sẵn phân vùng 8MB.
2. Điện thoại vào WiFi **DuoiSoc** (mật khẩu `12345678`), mở `http://192.168.4.1`.
3. Thẻ **Tình trạng thiết bị** → nút **Internet**:
   - Tên + mật khẩu WiFi nhà vườn (**2,4 GHz**). Mật khẩu chỉ lưu trong board, không nằm trong code.
   - **Mã PIN** 4–8 số để điều khiển từ xa.
   - Bấm **Lưu**. Khoảng 30 giây sau mở lại mục Internet: thấy *"Đã vào WiFi … · Internet: đang gửi lên Internet"* là xong.
4. Mở **https://duoi-soc-thong-minh.vercel.app** ở bất cứ đâu. Góc trên hiện **"Từ xa · vừa cập nhật"**. Lần đầu bấm nút điều khiển, web hỏi mã PIN.

## Ghi chú

- **Ảnh camera trên web từ xa** là ảnh chụp 5 giây/lần (không phải video), chỉ gửi khi có người đang xem để tiết kiệm dung lượng.
- **Lệnh từ xa** tới thiết bị sau vài giây; nếu vừa mở web thì lần đầu có thể chậm tới khoảng 1 phút.
- **Đổi cài đặt** (ngưỡng điểm, thời gian xua...) chỉ làm được trên web của thiết bị trong vườn; web từ xa chỉ xem.
- **Giờ** được lấy tự động qua Internet (giờ Việt Nam), không cần đồng bộ từ điện thoại.
- **Bảo mật:** thiết bị tự sinh **khóa thiết bị** ngẫu nhiên lần đầu gửi lên; web chỉ nhận dữ liệu từ đúng khóa đó. Lệnh điều khiển cần PIN, sai 10 lần thì khóa 10 phút. Ai có link web đều **xem** được trạng thái và ảnh.
- **Đổi sang board khác** (khóa thiết bị khác): xóa khóa cũ trong kho Upstash (khóa `ds:devkey`) rồi board mới gửi lên sẽ được nhận.
- **Hạn mức miễn phí** Upstash khoảng 500.000 lệnh/tháng: đủ cho 1 thiết bị gửi 60 giây/lần cả tháng và xem vài giờ mỗi ngày.

## Các API (web/api)

| API | Ai gọi | Việc |
|---|---|---|
| `POST /api/push` | thiết bị | gửi trạng thái + sự kiện, nhận lại lệnh từ xa và cờ "có người xem" |
| `POST /api/push-photo?id=N` / `?live=1` | thiết bị | gửi ảnh sóc (giữ 7 ngày) / ảnh trực tiếp (giữ 2 phút) |
| `GET /api/status`, `/api/events`, `/photo?id=N`, `/capture` | web | giống hệt API của thiết bị, nên giao diện dùng chung một code |
| `POST /api/control` | web | lệnh điều khiển (cần PIN) |
| `POST /api/ai?keep=0/1` | thiết bị | ảnh camera → điểm sóc AI (YOLO26n, ONNX Runtime WebAssembly, `api/_yolo.js`) |
