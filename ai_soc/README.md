# AI YOLO26n nhận diện sóc

Mô hình AI tự huấn luyện để nhận ra con sóc trong ảnh camera ESP32-S3. Cách làm giống phần `ai_camera` của dự án Huỳnh Thái Nguyên: thu ảnh có giấy phép tự do → gắn nhãn tự động → huấn luyện YOLO26n trên GPU.

```text
Wikimedia Commons (2384 ảnh)  →  Grounding DINO + CLIP gắn nhãn  →  YOLO26n học 50 vòng (RTX 4050)  →  soc_yolo26n.onnx
                                                                                                          │
ESP32-S3 CAM: PIR báo → chụp ảnh → POST /api/ai (Vercel, ONNX Runtime WebAssembly) → điểm sóc 0–100 ─────┘
              điểm ≥ ngưỡng (mặc định 60) → xua đuổi
```

## Kết quả

Chấm trên ảnh **AI chưa từng học**:

| Bộ ảnh | Độ chính xác (precision) | Tìm được sóc (recall) | mAP50 | mAP50-95 |
|---|---|---|---|---|
| Kiểm tra (val, 168 ảnh) | 90,7% | 79,5% | 88,5% | 69,5% |
| **Thi cuối (test, 167 ảnh)** | **95,0%** | **87,4%** | **94,6%** | **78,9%** |

Theo từng ảnh (335 ảnh val + test: 261 ảnh có sóc, 74 ảnh nền khó gồm chim, chuột, mèo trên cây):

| Ngưỡng tin cậy YOLO | Bắt đúng ảnh có sóc | Báo nhầm ảnh không sóc |
|---|---|---|
| 0,10 | 95,4% | 13,5% |
| **0,15 (= 60 điểm trên thiết bị)** | **95,0%** | **9,5%** |
| 0,25 | 93,5% | 6,8% |
| 0,50 | 88,1% | 2,7% |

- Chọn **0,15 = 60 điểm**: bỏ sót sóc thì mất trái, còn báo nhầm chỉ làm chim/mèo giật mình.
- Muốn ít báo nhầm hơn thì tăng ngưỡng điểm trên web thiết bị: 70 điểm ≈ tin cậy 0,36; 80 điểm ≈ 0,58.

Ảnh mẫu trong `web/img`:
- ảnh có sóc: 95–98 điểm;
- sóc treo ngược ăn trái (`sq_eat`): 60 điểm;
- cây/trái sầu riêng không sóc: 0–21 điểm.

Xem `logs/thu_mo_hinh.jpg` và `logs/ket_qua_huan_luyen.png`.

**Tốc độ:**
- Trên Vercel: AI chạy khoảng 0,5 giây; cả lượt gọi khoảng 1,6 giây (lần đầu sau lúc nghỉ khoảng 4 giây).
- Trước đây MobileNet mất 2–5 giây.

## Dữ liệu

- **2384 ảnh** tải từ Wikimedia Commons (`tools/thu_anh_commons.py`):
  - sóc Việt Nam / Đông Nam Á: *Callosciurus*, *Ratufa*, *Tamiops*, *Menetes*, *Dremomys*, *Funambulus*;
  - sóc phổ biến: *Sciurus*;
  - ảnh nền: cây sầu riêng, vườn cây ăn trái, chim chào mào, chuột, mèo trên cây.
- Gắn nhãn tự động (`tools/gan_nhan.py`):
  - Grounding DINO khoanh "squirrel";
  - CLIP loại tranh vẽ, thú nhồi bông, tem, tiêu bản, ảnh đen trắng.
- Ảnh nền mà máy thấy có sóc thì bỏ.
- Kết quả: **1673 ảnh** (1251 ảnh sóc với 1332 khung, 422 ảnh nền không sóc), chia 1338 train / 168 val / 167 test.
- Đã xem ngẫu nhiên 20 ảnh: khung đúng cả 20 (`logs/preview_ngau_nhien.jpg`).
- **Ghi nguồn:** `NGUON_ANH.csv` liệt kê tác giả, giấy phép, link gốc của từng ảnh đã dùng. Phần lớn là CC BY-SA / CC BY / CC0 / Public domain. Khi trình bày cần ghi "Ảnh huấn luyện: Wikimedia Commons, xem NGUON_ANH.csv".

## Chạy lại

```bash
cd ai_soc/tools
python thu_anh_commons.py --max-per-source 120   # tải ảnh (chạy lại bỏ qua ảnh đã có)
python gan_nhan.py                               # gắn nhãn (bị dừng giữa chừng: thêm --resume)
python huan_luyen.py --epochs 50                 # học trên GPU, ra models/soc_yolo26n.pt + .onnx
python thu_mo_hinh.py                            # thử trên ảnh web/img
python ghi_nguon.py                              # lập NGUON_ANH.csv
```

Đưa mô hình mới lên web:
1. Chép `models/soc_yolo26n.onnx` vào `web/models/`.
2. Deploy Vercel trong thư mục `web`.

Nếu thư mục `web/models/` không có file thì `/api/ai` tự dùng MobileNet cũ.

**Lưu ý laptop:** cắm **sạc zin** khi gắn nhãn hoặc huấn luyện. Chạy pin hay sạc yếu thì card RTX 4050 bị giới hạn ở 210 MHz, chậm gấp 6 lần.

| Thư mục | Nội dung |
|---|---|
| `raw_images/` | ảnh gốc + `metadata.csv` |
| `dataset/` | ảnh + nhãn YOLO, `data.yaml` |
| `preview/` | ảnh đã vẽ khung để kiểm tra bằng mắt |
| `runs/soc_yolo26n/` | kết quả huấn luyện, biểu đồ, `metrics_summary.json` |
| `models/` | `soc_yolo26n.pt` (Python), `soc_yolo26n.onnx` (web) |
