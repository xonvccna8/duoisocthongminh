# SƠ ĐỒ ĐI DÂY - HỆ THỐNG ĐUỔI SÓC THÔNG MINH

Board điều khiển: **Freenove ESP32-S3-WROOM CAM (N8R8)**, đã có sẵn camera và khe thẻ nhớ.

---

## 0. Danh sách linh kiện cần chuẩn bị để hàn

| # | Linh kiện | SL | Ghi chú |
|---|---|---|---|
| 1 | Freenove ESP32-S3-WROOM CAM (N8R8) + camera OV2640 | 1 | |
| 2 | Cảm biến PIR HC-SR501 | 1 | Hoặc AM312 (nhỏ, cấp 3.3V) |
| 3 | Module RTC DS3231 | 1 | Lắp pin CR2032 |
| 4 | Module cảm biến ánh sáng BH1750 (GY-302) | 1 | |
| 5 | Module đo dòng/áp INA219 | 1 | |
| 6 | DFPlayer Mini + thẻ microSD (≤32GB, FAT32) | 1 | |
| 7 | Loa 4Ω/8Ω, 3W | 1 | |
| 8 | Đèn LED 12V (5–10W, loại có sẵn driver/thanh LED 12V) | 1 | |
| 9 | Module phun sương siêu âm 5V (USB mist module) | 1 | |
| 10 | MOSFET logic-level **IRLZ44N** (hoặc AO3400, module D4184) | 2 | ⚠ KHÔNG dùng IRF520, vì nó không mở đủ ở 3.3V |
| 11 | Điện trở 220Ω | 2 | Nối vào chân Gate |
| 12 | Điện trở 10kΩ | 2 | Kéo Gate xuống GND |
| 13 | Điện trở 1kΩ | 1 | Dây TX đi sang DFPlayer RX |
| 14 | Diode 1N5819 (hoặc 1N4007) | 2 | Chống dòng ngược cho tải |
| 15 | Mạch hạ áp LM2596 (3A) | 1 | Chỉnh ra **5.0V** |
| 16 | Tụ hóa 470–1000µF/16V | 2 | Một tụ ở ngõ 5V, một tụ cạnh DFPlayer |
| 17 | Tụ gốm 100nF | 3 | |
| 18 | Pin 3S 18650 (12V) + mạch BMS 3S, **hoặc** ắc quy 12V 7Ah | 1 | Lúc test trên bàn có thể dùng adapter 12V 2A |
| 19 | Tấm pin mặt trời 18V 20W + bộ sạc (solar charge controller) | 1 | |
| 20 | Cầu chì 3A + đế, công tắc nguồn | 1 | |
| 21 | Domino, dây điện, PCB đục lỗ, header cái | – | |

---

## 1. Sơ đồ tổng thể

```
  ┌──────────────┐     ┌────────────────┐     ┌──────────┐   ┌─────────┐
  │ PIN MẶT TRỜI │────►│  BỘ SẠC SOLAR  │◄───►│ PIN 12V  │   │CẦU CHÌ  │
  │   18V 20W    │     │ (charge ctrl)  │     │ (3S/AQ)  │   │  3A     │
  └──────────────┘     └───────┬────────┘     └──────────┘   └────┬────┘
                               │ LOAD OUT 12V ────► CÔNG TẮC ─────┘
                               ▼
                        ┌─────────────┐
                        │   INA219    │  VIN+ ◄── 12V từ công tắc
                        │ (đo pin)    │  VIN- ──► BUS 12V của hệ thống
                        └──────┬──────┘
                               │ BUS 12V
            ┌──────────────────┼─────────────────────────┐
            ▼                  ▼                         ▼
     ┌─────────────┐    ┌─────────────┐           ┌─────────────┐
     │  LM2596     │    │ ĐÈN LED 12V │           │  (dự phòng) │
     │ 12V → 5.0V  │    │ qua MOSFET1 │           │             │
     └──────┬──────┘    └─────────────┘           └─────────────┘
            │ BUS 5V (+ tụ 1000µF)
   ┌────────┼───────────┬──────────────┬──────────────────┐
   ▼        ▼           ▼              ▼                  ▼
 ESP32-S3  DFPlayer   PIR HC-SR501   PHUN SƯƠNG 5V    (I2C dùng 3.3V
 (chân 5V)  (VCC)      (VCC)         qua MOSFET2       từ ESP32)

 ★ TẤT CẢ GND NỐI CHUNG MỘT ĐIỂM (GND pin, LM2596, ESP32, MOSFET, module)
```

---

## 2. Bảng đấu nối chân ESP32-S3

| Chân ESP32-S3 | Nối tới | Chức năng |
|---|---|---|
| **5V** | BUS 5V (ngõ ra LM2596) | Nguồn board |
| **3V3** | VCC của DS3231, BH1750, INA219 | Nguồn I2C |
| **GND** | GND chung | |
| **GPIO 14** | PIR → OUT | Phát hiện chuyển động |
| **GPIO 21** | SDA của DS3231 + BH1750 + INA219 | I2C SDA |
| **GPIO 47** | SCL của DS3231 + BH1750 + INA219 | I2C SCL |
| **GPIO 42** | → điện trở 1kΩ → DFPlayer **RX** | UART TX |
| **GPIO 41** | ← DFPlayer **TX** | UART RX |
| **GPIO 1** | → 220Ω → Gate MOSFET 1 (đèn LED) | Bật/tắt đèn |
| **GPIO 48** | → 220Ω → Gate MOSFET 2 (phun sương) | Bật/tắt phun sương |
| Camera, thẻ SD | Có sẵn trên board | Không cần đi dây |

> Các chân **KHÔNG được dùng**: 4–13, 15–18 (camera), 38–40 (thẻ SD), 35–37 (PSRAM), 19–20 (USB), 43–44 (Serial), 0/45/46 (chân strapping, dùng sai sẽ không boot được).

---

## 3. Chi tiết từng module

### 3.1 Cảm biến PIR HC-SR501
```
 HC-SR501            ESP32-S3
 ┌────────┐
 │  VCC   ├──────── 5V
 │  OUT   ├──────── GPIO 14      (ngõ ra 3.3V, nối thẳng được)
 │  GND   ├──────── GND
 └────────┘
```
- Jumper chế độ: đặt **H (repeat trigger)**.
- Biến trở **Tx** (thời gian giữ): vặn **ngược chiều kim đồng hồ hết cỡ** (~3s).
- Biến trở **Sx** (độ nhạy): để giữa.
- Nếu dùng AM312 thì cấp **3V3**, các chân khác nối giống trên.

### 3.2 Bus I2C: DS3231 + BH1750 + INA219 (đấu song song)
```
                 3V3 ───┬─────────────┬──────────────┐
                        │             │              │
 ESP32 GPIO21 (SDA) ────┼──SDA────────┼──SDA─────────┼──SDA
 ESP32 GPIO47 (SCL) ────┼──SCL────────┼──SCL─────────┼──SCL
                        │             │              │
                    [DS3231]      [BH1750]       [INA219]
                     VCC,GND     VCC,GND,ADDR→GND  VCC,GND
                        │             │              │
                 GND ───┴─────────────┴──────────────┘
```
- Các module này đã có sẵn trở kéo lên 4.7k–10k nên **không cần gắn thêm**.
- Dây I2C nên ngắn hơn 30cm. Nếu dài hơn, xoắn SDA với GND.
- Địa chỉ mong đợi khi quét I2C: **0x23** (BH1750), **0x40** (INA219), **0x57** (EEPROM trên DS3231), **0x68** (DS3231).

### 3.3 INA219: đo áp và dòng của pin (mắc nối tiếp với đường dương)
```
  + PIN 12V ──► CÔNG TẮC ──► [VIN+  INA219  VIN-] ──► BUS 12V (vào LM2596 + LED)
  - PIN 12V ─────────────────── GND chung
```
- **GND của INA219 phải nối chung với GND pin**, nếu không sẽ đọc sai điện áp.
- Điện trở shunt 0.1Ω đo tối đa ±3.2A.

### 3.4 DFPlayer Mini + loa
```
                    DFPlayer Mini
                  ┌──────────────┐
   5V ────────────┤ VCC          │
   GPIO42 ─[1kΩ]─►┤ RX           │
   GPIO41 ◄───────┤ TX           │
                  │              │
   Loa (+) ───────┤ SPK_1        │
   Loa (−) ───────┤ SPK_2        │
   GND ───────────┤ GND (2 chân) │
                  └──────────────┘
```
Bảng đấu (theo tên chân in trên module):

| DFPlayer | Nối tới |
|---|---|
| VCC | 5V (gắn tụ 470µF + 100nF sát chân VCC–GND) |
| GND (cả 2 chân GND) | GND |
| **RX** | ← **1kΩ** ← GPIO 42 (TX của ESP32) |
| **TX** | → GPIO 41 (RX của ESP32) |
| SPK_1 | Loa (+) |
| SPK_2 | Loa (−) |

- ⚠ **SPK_1/SPK_2 không được nối GND.** Loa nối thẳng vào hai chân này, mạch khuếch đại 3W đã có sẵn bên trong.
- Thẻ nhớ format **FAT32**. Chép file vào thư mục gốc, đặt tên `0001.mp3`, `0002.mp3`, `0003.mp3`... (tiếng chó sủa, tiếng diều hâu, tiếng nổ, tiếng siêu âm...). Nên **xóa thẻ rồi chép lần lượt từng file theo thứ tự**.

### 3.5 Mạch công suất bằng MOSFET (dùng cho cả đèn LED và phun sương)
```
                       + Nguồn tải (12V cho LED / 5V cho phun sương)
                          │
                    ┌─────┴─────┐
                    │           │
                  [TẢI]      ──┴── Diode 1N5819
                    │         ▲    (vạch trắng/catot quay về phía +)
                    │         │
                    ├─────────┘
                    │
                    D (Drain - chân giữa)
 GPIO ──[220Ω]──┬── G (Gate - chân trái)       IRLZ44N
                │   S (Source - chân phải) ──┐ (mặt chữ hướng về mình)
             [10kΩ]                          │
                │                            │
 GND ───────────┴────────────────────────────┴──── GND chung
```

| Kênh | GPIO | Nguồn tải | Tải |
|---|---|---|---|
| MOSFET 1 | GPIO 1 | BUS **12V** | Đèn LED 12V |
| MOSFET 2 | GPIO 48 | BUS **5V** | Module phun sương 5V |

- Trở 10kΩ kéo Gate xuống GND giữ cho tải **không tự bật khi ESP32 khởi động hoặc reset**.
- Đèn LED công suất trên 5W thì gắn tản nhiệt nhỏ cho MOSFET.
- Nếu dùng module phun sương có mạch driver và nút bấm riêng, nối MOSFET vào **đường GND cấp nguồn cho module**. Có loại module cần bấm nút mới chạy; khi đó hàn tắt nút (nối tắt 2 chân nút) để module tự chạy ngay khi có điện.

### 3.6 Nguồn
1. **Trước khi nối vào ESP32:** cấp 12V vào LM2596, dùng đồng hồ đo rồi vặn biến trở cho ngõ ra đúng **5.0V–5.1V**.
2. Gắn tụ 1000µF ở ngõ ra 5V.
3. Đi dây nguồn 12V cho đèn LED **trực tiếp từ BUS 12V** bằng dây to (≥0.5mm²), không lấy qua board ESP32.
4. Gắn cầu chì 3A ngay sau cực + của pin.

---

## 4. Thứ tự hàn và kiểm tra (làm lần lượt, không cắm hết một lúc)

| Bước | Việc làm | Kiểm tra |
|---|---|---|
| 1 | Hàn khối nguồn: cầu chì, công tắc, INA219, LM2596 | Đo ra 5.0V, **chưa cắm ESP32** |
| 2 | Cắm ESP32 qua USB, nạp code test | Serial in ra menu |
| 3 | Nối bus I2C (3 module) | Gõ `1` thấy 0x23, 0x40, 0x57, 0x68 |
| 4 | Gõ `t` | RTC đã được đặt giờ |
| 5 | Nối PIR | Gõ `2`, vẫy tay thì thấy HIGH |
| 6 | Nối DFPlayer + loa | Gõ `6` phát tiếng, gõ `+`/`-` để chỉnh âm lượng |
| 7 | Hàn MOSFET 1 + đèn LED | Gõ `7` đèn sáng/tắt |
| 8 | Hàn MOSFET 2 + phun sương | Gõ `8` phun sương, tự tắt sau 10s |
| 9 | Camera | Gõ `9` chụp ảnh, gõ `w` rồi dùng điện thoại vào WiFi `DuoiSoc-Test` (mk `12345678`) và mở `http://192.168.4.1` |
| 10 | Rút USB, cấp nguồn 12V | Gõ `a` để test tự động (cần cắm USB lại để xem log) |
| 11 | Gõ `d` | Demo: vẫy tay trước PIR → chụp ảnh + LED + loa + phun sương 5s |

⚠ **Không cấp đồng thời USB và 5V từ LM2596 khi board không có diode chống ngược.** Khi test bằng USB, rút dây 5V từ LM2596 ra (vẫn giữ dây GND chung).

---

## 5. Lỗi thường gặp

| Hiện tượng | Nguyên nhân / Cách xử lý |
|---|---|
| ESP32 reset liên tục khi loa kêu hoặc LED bật | Nguồn sụt áp: thêm tụ 1000µF, dùng dây nguồn to hơn, kiểm tra LM2596 |
| DFPlayer báo `LOI` | Đảo dây TX/RX, thiếu trở 1k, thẻ không phải FAT32, tên file sai |
| DFPlayer kêu "bụp" và rè | Gắn tụ 470µF sát VCC, tách dây GND loa khỏi GND tín hiệu |
| Quét I2C không thấy module nào | Nhầm SDA/SCL, module chưa có nguồn 3V3, chưa nối GND chung |
| PIR báo liên tục | Chưa "làm nóng" xong (chờ 60s), độ nhạy để cao quá, bị nắng/gió chiếu thẳng vào |
| Camera báo lỗi 0x105/0x20004 | Cáp camera chưa cắm chặt, chưa chọn PSRAM "OPI PSRAM" |
| LED/phun sương tự bật lúc khởi động | Thiếu trở 10kΩ kéo Gate xuống GND |
| MOSFET nóng, đèn LED sáng yếu | Đang dùng IRF520, cần đổi sang IRLZ44N/AO3400 |
| INA219 đọc 0V | VIN+/VIN- chưa nối nối tiếp với đường dương, hoặc chưa nối GND chung |
