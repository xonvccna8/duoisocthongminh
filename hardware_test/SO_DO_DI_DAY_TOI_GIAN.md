# SƠ ĐỒ ĐI DÂY BẢN TỐI GIẢN - HỆ THỐNG ĐUỔI SÓC

Bản này dùng ít linh kiện nhất mà vẫn chạy đủ chức năng chính: **phát hiện chuyển động → chụp ảnh → phát âm thanh + phun mùi**, có web giám sát.
Không có đồng hồ DS3231, cảm biến ánh sáng BH1750, mạch đo pin INA219 và đèn LED. Firmware vẫn chạy bình thường; trên web các mục đó chỉ hiện **"Lỗi"**. Giờ được lấy từ điện thoại khi mở web.

Hình vẽ: [SO_DO_DI_DAY_TOI_GIAN.png](SO_DO_DI_DAY_TOI_GIAN.png) (bản gốc [SVG](SO_DO_DI_DAY_TOI_GIAN.svg)).
Code giữ nguyên, không cần sửa chân nào trong `config.h`.

---

## 0. Linh kiện

### Đã có

| # | Linh kiện | Ghi chú |
|---|---|---|
| 1 | Freenove ESP32-S3-WROOM CAM (N8R8) | Có sẵn camera và khe thẻ nhớ |
| 2 | Cảm biến PIR **AM312 mini** | Cấp 3,3V; chân giữa là OUT |
| 3 | Module phun sương siêu âm 5V | |
| 4 | Loa 4Ω 3W | |

### Bắt buộc mua thêm

| # | Linh kiện | SL | Vì sao cần |
|---|---|---|---|
| 5 | **DFPlayer Mini** + thẻ microSD (≤ 32GB, FAT32) | 1 | ESP32 **không** kéo loa trực tiếp được. DFPlayer vừa chứa file tiếng kêu vừa có sẵn mạch khuếch đại 3W cho loa 4Ω |
| 6 | MOSFET logic-level **IRLZ44N** (chân cắm TO-220, dễ hàn) | 1 | Để ESP32 bật/tắt phun sương. ⚠ **Không** dùng IRF520 vì không mở đủ ở 3,3V. Có thể thay bằng module MOSFET D4184 |
| 7 | Điện trở 220Ω | 1 | Nối chân Gate MOSFET |
| 8 | Điện trở 10kΩ | 1 | Kéo Gate xuống GND, giúp phun sương không tự bật lúc khởi động |
| 9 | Điện trở 1kΩ | 1 | Nối dây TX của ESP32 sang RX của DFPlayer (giảm nhiễu "rè") |
| 10 | Diode 1N5819 (hoặc 1N4007) | 1 | Chống dòng ngược từ module phun sương |
| 11 | Tụ hóa 1000µF / 16V | 1 | Giữ ổn định nguồn 5V |
| 12 | Tụ hóa 470µF / 16V | 1 | Đặt sát DFPlayer, chống tiếng "bụp" và chống reset |
| 13 | Tụ gốm 100nF (104) | 2 | 1 cái cho DFPlayer, 1 cái cho AM312 |
| 14 | **Adapter 5V 3A** (tối thiểu 2A) + jack DC cái + công tắc | 1 | Nguồn chung cho cả hệ thống |
| 15 | PCB đục lỗ, header cái, dây điện, domino | – | |
| 16 | *(Tùy chọn)* Màn hình **OLED 0,96" I2C** (SSD1306, 4 chân) | 1 | Hiện trạng thái, số lần thấy sóc và địa chỉ web ngay trên thiết bị. Xem mục 3.7 |

> Vì sao cần nguồn 3A: ESP32 + camera + WiFi dùng khoảng 0,5A; DFPlayer kéo loa 4Ω lúc to nhất khoảng 0,7A; phun sương khoảng 0,3–0,5A. Tổng gần 1,7A. Nguồn yếu sẽ làm ESP32 **reset mỗi khi loa kêu**.

---

## 1. Sơ đồ tổng thể

```
 ADAPTER 5V 3A ──► JACK DC ──► CÔNG TẮC ──┬──────── BUS +5V ────────────────────────────┐
                                          │                                             │
                                     [1000µF]                                           │
                                          │                                             │
              ┌───────────────────────────┼─────────────────────┐                       │
              ▼                           ▼                     ▼                       ▼
      ESP32-S3 (chân 5V)          DFPlayer (VCC)        Phun sương (+)        (diode 1N5819
              │                     + 470µF + 100nF             │              song song phun sương)
      ESP32 3V3 ──► AM312 VCC                           Phun sương (−)
                                                                │
                                                        MOSFET IRLZ44N (D)

 ★ TẤT CẢ GND NỐI CHUNG MỘT ĐIỂM: adapter, ESP32, AM312, DFPlayer, nguồn MOSFET (S)
```

---

## 2. Bảng đấu nối chân ESP32-S3

| Chân ESP32-S3 | Nối tới | Chức năng |
|---|---|---|
| **5V** | BUS +5V | Nguồn cho board |
| **GND** | GND chung | |
| **3V3** | AM312 **VCC** | Nguồn cảm biến |
| **GPIO 14** | AM312 **OUT** | Phát hiện chuyển động |
| **GPIO 42** | MAX98357A **BCLK** | Loa (I2S) |
| **GPIO 41** | MAX98357A **LRC** | Loa (I2S) |
| **GPIO 2** | MAX98357A **DIN** | Loa (I2S) |
| **GPIO 48** | → điện trở **220Ω** → **Gate** MOSFET | Bật/tắt phun sương |
| **GPIO 21** | OLED **SDA** *(tùy chọn)* | Dữ liệu I2C |
| **GPIO 47** | OLED **SCL** *(tùy chọn)* | Xung nhịp I2C |
| Camera, thẻ nhớ | Có sẵn trên board | Không cần đi dây |

> Không dùng các chân: 4–13, 15–18 (camera), 38–40 (thẻ nhớ), 35–37 (PSRAM), 19–20 (USB), 43–44 (Serial), 0/45/46 (chân khởi động).
> GPIO 48 cũng nối với đèn RGB nhỏ trên board, nên đèn này có thể sáng khi phun sương. Hiện tượng này bình thường.

---

## 3. Chi tiết từng phần

### 3.1 Cảm biến PIR AM312 mini

Hình vẽ: [NOI_DAY_PIR.png](NOI_DAY_PIR.png).

```
  AM312 (nhìn từ phía chân)        ESP32-S3
  ┌──────────────┐
  │  VCC  (+)    ├──────────────── 3V3
  │  OUT  (giữa) ├──────────────── GPIO 14
  │  GND  (−)    ├──────────────── GND
  └──────────────┘
        └─ tụ 100nF hàn thẳng giữa VCC và GND, sát chân cảm biến
```

- **Xem chữ in trên mạch** để biết chân nào là VCC, chân nào là GND. Chân giữa luôn là OUT. Nối ngược VCC/GND có thể làm hỏng cảm biến.
- Cấp **3V3** (không cấp 5V). Ngõ ra là 3,3V nên nối thẳng vào GPIO được.
- AM312 **rất nhạy với sóng WiFi**: đặt cảm biến cách anten ESP32 **ít nhất 10cm**, dùng dây dài 15–30cm. Nếu cảm biến báo liên tục khi không có ai, nguyên nhân thường là nhiễu này.
- Sau khi cấp điện, cần chờ khoảng **30–60 giây** để cảm biến ổn định. AM312 không có biến trở chỉnh: tầm phát hiện khoảng 3–5m, giữ tín hiệu khoảng 2 giây.

### 3.2 Loa 4Ω 3W qua MAX98357A (firmware hiện tại)

Hình vẽ: [NOI_DAY_LOA_MAX98357A.png](NOI_DAY_LOA_MAX98357A.png). Firmware `duoi_soc_web` giờ phát âm thanh qua mạch khuếch đại I2S **MAX98357A**, đọc file WAV trên **khe thẻ nhớ của board ESP32**. Không cần DFPlayer, không cần điện trở.

| ESP32-S3 | MAX98357A |
|---|---|
| **GPIO 41** | LRC |
| **GPIO 42** | BCLK |
| **GPIO 2** | DIN |
| **GND** | GND |
| **5V** | VIN |
| – | GAIN, SD: để trống |
| – | Cọc **+** / **−** → 2 dây loa 4Ω |

- File âm thanh: thẻ microSD của board, thư mục `sound`, tên `1.wav`, `2.wav`… đánh số liên tục từ 1. Định dạng **WAV 16-bit PCM**, nên dùng **mono 16000 Hz** (file nhỏ, đủ nghe). Không dùng MP3.
- Đổi MP3 → WAV bằng **Audacity**: mở file → *Tracks › Mix › Mix Stereo Down to Mono* → góc dưới chọn *Project Rate* **16000** → *File › Export › Export as WAV* → *Signed 16-bit PCM*.
- Cọc loa **+ / −** không được nối GND hay nguồn. GPIO 2 nối cả đèn LED trên board, đèn nháy khi phát tiếng là bình thường.
- Sketch `hardware_test` vẫn dùng DFPlayer cho bài test loa (phím `6`); với MAX98357A hãy thử loa bằng nút phát âm thanh trên web.

### 3.2b (Cách cũ) DFPlayer Mini + loa 4Ω 3W

Hình vẽ: [NOI_DAY_LOA.png](NOI_DAY_LOA.png).

```
                    DFPlayer Mini
                  ┌──────────────┐
   BUS +5V ───────┤ VCC          │   (tụ 470µF + 100nF sát chân VCC–GND)
   GPIO42 ─[1kΩ]─►┤ RX           │
   GPIO41 ◄───────┤ TX           │
                  │        SPK_1 ├────── Loa (+)
                  │        SPK_2 ├────── Loa (−)
   GND ───────────┤ GND          │
                  └──────────────┘
```

- ⚠ **SPK_1 và SPK_2 không được nối GND**, cũng không nối chung với nguồn. Loa chỉ nối vào đúng 2 chân này.
- Thẻ nhớ của DFPlayer dùng định dạng **FAT32**. Chép file vào thư mục gốc, đặt tên `0001.mp3`, `0002.mp3`, `0003.mp3`... (tiếng chó sủa, diều hâu, tiếng nổ...). Nên format thẻ rồi chép **lần lượt từng file**, vì DFPlayer đánh số file theo thứ tự chép.
- Loa 4Ω kéo dòng lớn. Nếu ESP32 reset khi loa kêu, giảm **âm lượng xuống khoảng 20–24** trên web.

### 3.3 Phun sương qua MOSFET IRLZ44N

```
                 BUS +5V
                    │
          ┌─────────┴─────────┐
          │                   │
   [PHUN SƯƠNG (+)]      ──┴── Diode 1N5819
   [PHUN SƯƠNG (−)]        ▲   (vạch trắng quay về phía +5V)
          │                │
          ├────────────────┘
          │
          D  (chân giữa)
 GPIO48 ─[220Ω]─┬─ G  (chân trái)        IRLZ44N
                │   S  (chân phải) ──┐   (mặt có chữ hướng về phía mình)
             [10kΩ]                  │
                │                    │
 GND ───────────┴────────────────────┴──── GND chung
```

**Kiểm tra module phun sương trước khi hàn:**

1. Cấp 5V trực tiếp vào module (không qua MOSFET).
2. **Sương ra ngay mà không cần bấm** → nối như sơ đồ trên.
3. **Phải bấm nút sương mới ra** → hàn tắt 2 chân nút bằng một đoạn dây, rồi cấp điện lại:
   - Sương ra ngay → giữ dây hàn tắt đó và nối như sơ đồ trên.
   - Vẫn không ra (module bật/tắt theo kiểu nhấn–nhả) → **tháo dây nối tắt** và báo lại để sửa firmware sang cách "bấm nút điện tử" bằng opto PC817.
- **Module cổng Type-C** (mạch xanh + đĩa gốm + tăm bông): lấy điện 5V theo một trong hai cách:
  - Cắt một dây cáp USB → Type-C, cắm đầu Type-C vào mạch. Dây **đỏ** là (+), dây **đen** là (−); các dây khác bỏ, quấn băng keo.
  - Hoặc hàn thẳng vào 2 chân nguồn to ở phía sau cổng Type-C (VBUS = +, GND = −). Đo bằng đồng hồ trước khi hàn để chắc chắn.

### 3.4 Lắp module phun sương Type-C (mạch + tăm bông + đĩa gốm)

```
            sương bay ra ↑ (ra ngoài hộp, tránh camera)
        ┌─────────────────────┐
        │   ĐĨA GỐM (mặt lồi  │ ◄── kẹp nhựa đen giữ đĩa áp sát đầu tăm bông
        │   hướng ra ngoài)   │
        └─────────┬───────────┘
   nắp lọ ═══════╪═══════  (khoan lỗ vừa khít tăm bông)
                  │ tăm bông
     ┌────────────┼────────────┐
     │  nước + vài │ giọt tinh │   lọ nhựa 50–100ml
     │  dầu        │           │   (tăm bông chạm đáy lọ)
     └─────────────┴───────────┘
        dây đĩa gốm ≤ 20cm ──► MẠCH PHUN SƯƠNG (đặt TRONG hộp)
```

1. Cắm tăm bông vào lọ nước sao cho **chạm đáy**. Đợi 3–5 phút để tăm ngấm nước lên tận đầu.
2. Đặt đĩa gốm lên đầu tăm bông và kẹp bằng 2 miếng nhựa đen. Đĩa phải **áp sát** tăm bông.
3. **Mặt đĩa có phần lồi ở giữa hướng ra ngoài** (phía sương bay ra). Nếu sương ra rất ít, lật ngược đĩa lại rồi thử.
4. Cắm dây đĩa gốm vào cổng 2 chân trên mạch.

⚠ **Không bao giờ chạy khi đĩa khô** (lọ hết nước hoặc tăm bông chưa ngấm) vì đĩa sẽ nóng và hỏng nhanh.
⚠ Không dùng tinh dầu nguyên chất: chỉ pha **3–5 giọt tinh dầu cho 100ml nước**, nếu không lỗ đĩa sẽ bị tắc. Khoảng 2 tuần lau đĩa bằng giấm hoặc cồn một lần.

**Gắn trong hộp hay ngoài hộp?**

| Bộ phận | Vị trí | Lý do |
|---|---|---|
| Mạch phun sương (mạch xanh) | **Trong hộp**, cạnh ESP32 | Tránh mưa và hơi nước |
| Lọ nước + tăm bông + đĩa gốm | **Ngoài hộp** (gắn sát thành hộp, có mái che mưa nhỏ) hoặc trong một ngăn riêng có lỗ để đĩa thò ra ngoài | Hơi nước đọng trong hộp kín sẽ làm ẩm và chập mạch ESP32 |
| Dây đĩa gốm | Luồn qua lỗ ở thành hộp, bịt lỗ bằng keo nến/ốc siết cáp, dài **≤ 20cm** | Dây dài làm sương yếu |

- Hướng sương **ra xa camera và cảm biến PIR** (đặt lệch hướng hoặc thấp hơn camera, cách ít nhất 20–30cm), nếu không ống kính camera sẽ bị mờ hơi nước.
- Đặt lọ **thẳng đứng** và để đĩa gốm ở trên cùng. Thiết bị nên gắn trên thân cây sầu riêng, cao khoảng 1,5–2m, ngay đường sóc hay leo, sao cho sương tỏa về phía thân và cành.
- Mỗi lần xua đuổi hệ thống chỉ phun vài giây (mặc định 8 giây), nên lọ 100ml dùng được khá lâu. Dù vậy vẫn nên kiểm tra và châm nước mỗi tuần.

### 3.5 Nguồn

1. Adapter 5V → jack DC → công tắc → **BUS +5V**. Gắn tụ 1000µF giữa BUS +5V và GND (chân dài/không có vạch là +).
2. Dùng dây to (≥ 0,5mm²) cho BUS +5V và GND. **Không lấy nguồn cho loa và phun sương từ chân 5V của board ESP32**; lấy từ BUS.
3. ⚠ **Không cắm USB vào máy tính khi adapter 5V đang cấp điện.** Khi nạp code bằng USB, gạt công tắc tắt nguồn adapter (vẫn giữ dây GND chung).

### 3.6 Dùng pin lithium 3,7V (18650) thay adapter

Phun sương, DFPlayer và ESP32 đều cần **5V**, nên **không nối pin 3,7V thẳng vào BUS**. Cần một **mạch sạc + tăng áp 5V** (loại dùng làm sạc dự phòng). Từ ngõ ra 5V trở đi, mọi thứ nối **y hệt sơ đồ**: phun sương vẫn đi qua MOSFET.

```
                    ┌──────────────────────────────┐
  Sạc điện thoại ──►│ IN (Type-C / micro USB)      │
  hoặc pin mặt trời │                              │
  5V cổng USB       │  MẠCH SẠC + TĂNG ÁP 5V 2A    │
                    │  (IP5306 hoặc tương đương)   │
  Pin 18650 (+) ───►│ B+                    OUT+ 5V├──► CÔNG TẮC ──► BUS +5V (tụ 1000µF)
  Pin 18650 (−) ───►│ B−                    OUT−   ├──────────────► GND chung
                    └──────────────────────────────┘

  BUS +5V ──► Phun sương (+)        Phun sương (−) ──► MOSFET D   (giữ nguyên như mục 3.3)
          ──► ESP32 5V, DFPlayer VCC
```

| Linh kiện | Ghi chú |
|---|---|
| Mạch sạc + tăng áp **5V, tối thiểu 2A** (IP5306, hoặc "mạch sạc dự phòng 5V 2,1A") | ⚠ **Không dùng MT3608**: mạch này chỉ chịu khoảng 1A ở 5V, nên ESP32 sẽ reset khi loa và phun sương chạy cùng lúc |
| Pin 18650 có thương hiệu, 2–3 cell **mắc song song** | Các cell phải cùng loại và sạc đầy như nhau trước khi ghép |
| Hộp đựng pin hoặc hàn bằng kẽm hàn pin | Không hàn mỏ hàn lâu lên thân pin |

- **Thời gian chạy ước tính:** hệ thống tiêu thụ trung bình khoảng 1,3–1,5W (WiFi + camera luôn bật). Mỗi cell 3000mAh chạy được khoảng **6–7 giờ**; 3 cell song song chạy được khoảng **18–20 giờ**; pin **10000mAh** (3,7V ≈ 37Wh) chạy được khoảng **20–24 giờ** và sạc đầy mất khoảng 5–6 giờ với mạch 2A. Muốn chạy liên tục ngày đêm, cần thêm pin mặt trời có cổng ra USB 5V (6V, ≥ 5W) cắm vào cổng IN của mạch sạc.
- Mạch IP5306 tự ngắt khi tải dưới khoảng 50mA. ESP32 luôn dùng nhiều hơn mức đó nên không bị ảnh hưởng.
- ⚠ **An toàn pin lithium:** không đấu ngược cực, không để chập (+) và (−), có cầu chì 3A ở dây (+) của pin, không để pin ngoài nắng gắt (> 60°C) trong hộp kín.
- **Nếu chỉ muốn chạy riêng phun sương bằng pin** (không qua ESP32): pin → mạch sạc/tăng áp 5V → cổng Type-C của mạch phun sương. Một số mạch phun sương chạy được ở 3,7V, nhưng sương yếu hơn và không ổn định.

### 3.7 Màn hình OLED 0,96" (tùy chọn)

Hình vẽ: [NOI_DAY_OLED.png](NOI_DAY_OLED.png). Chỉ cần 4 dây, không cần điện trở:

| Dây | ESP32-S3 | OLED |
|---|---|---|
| 1 (đỏ) | **3V3** | **VCC** |
| 2 (đen) | **GND** | **GND** |
| 3 (vàng) | **GPIO 47** | **SCL** |
| 4 (xanh) | **GPIO 21** | **SDA** |

- ⚠ Nối theo **chữ in trên màn hình**, không theo vị trí: có loại xếp `GND VCC SCL SDA`, có loại xếp `VCC GND SCL SDA`. Nối nhầm VCC–GND sẽ hỏng màn hình.
- Firmware `duoi_soc_web` tự nhận màn hình (địa chỉ `0x3C`). Không gắn màn hình thì firmware vẫn chạy bình thường. Cần cài thêm thư viện **Adafruit SSD1306** (Arduino IDE sẽ hỏi cài kèm **Adafruit GFX**, chọn *Install all*).
- Màn hình hiển thị (chữ không dấu):

```
 DUOI SOC          14:32     ← giờ (có sau khi mở web trên điện thoại)
 ─────────────────────────
 CANH GAC                    ← CANH GAC / XUA DUOI! / DANG NGHI / DA TAT
 PIR:--  Diem cuoi 72        ← lúc xua đuổi: "Con 5s  Diem 85"
 Soc 3  Xua 2  CD 7          ← hôm nay: thấy sóc / xua đuổi / chuyển động
 192.168.4.1           PS    ← địa chỉ mở web; "PS" = đang phun sương
```

- Khi đang xua đuổi, màn hình nhấp nháy đảo màu để nhìn từ xa cũng thấy.
- Màn hình trắng/đen hoàn toàn: Serial Monitor báo `OLED:0` → kiểm tra dây SDA/SCL. Nếu vẫn không lên, đổi `ADDR_OLED` trong `config.h` từ `0x3C` thành `0x3D`.

---

## 4. Thứ tự hàn và kiểm tra

Nạp sketch `hardware_test` rồi mở Serial Monitor (115200). Hàn và thử **lần lượt từng bước**, không cắm hết một lúc.

| Bước | Việc làm | Kiểm tra |
|---|---|---|
| 1 | Hàn jack DC, công tắc, tụ 1000µF, BUS +5V/GND | Đo bằng đồng hồ: 5,0–5,2V, **chưa cắm ESP32** |
| 2 | Cắm ESP32 qua USB, nạp `hardware_test` | Serial in ra menu (gõ `h`) |
| 3 | Nối AM312 | Gõ `2`, vẫy tay trước cảm biến thì thấy **HIGH** (chờ 60s sau khi cấp điện) |
| 4 | Nối DFPlayer + loa | Gõ `6` để phát tiếng, gõ `+`/`-` để chỉnh âm lượng |
| 5 | Hàn MOSFET + phun sương | Gõ `8` để phun sương; module tự tắt sau 10s |
| 6 | Camera | Gõ `9` để chụp ảnh. Gõ `w`, vào WiFi `DuoiSoc-Test` (mật khẩu `12345678`) rồi mở `http://192.168.4.1` |
| 7 | Rút USB, cấp nguồn adapter | Gõ `d`: vẫy tay trước PIR → chụp ảnh + loa + phun sương trong 5s |
| 8 | Nạp `duoi_soc_web` | Vào WiFi `DuoiSoc`, mở `http://192.168.4.1` |

> Bước 1 trong menu (`1` – quét I2C) sẽ không thấy module nào. Kết quả này đúng, vì bản tối giản không có module I2C. Nếu có gắn OLED thì sẽ thấy địa chỉ **0x3C**.

---

## 5. Lỗi thường gặp

| Hiện tượng | Nguyên nhân / Cách xử lý |
|---|---|
| ESP32 reset khi loa kêu hoặc khi phun sương | Nguồn yếu: dùng adapter 3A, thêm tụ 1000µF, dùng dây to hơn, giảm âm lượng |
| PIR báo chuyển động liên tục | Nhiễu WiFi: để AM312 xa anten hơn và thêm tụ 100nF. Hoặc chưa chờ đủ 60s, hoặc bị nắng/gió nóng thổi thẳng vào |
| PIR không bao giờ báo | Nhầm chân (chân giữa là OUT), hoặc cấp nhầm 5V/ngược cực |
| DFPlayer không phát, Serial báo `LOI` | Đảo dây TX/RX, thiếu trở 1kΩ, thẻ không phải FAT32, tên file sai |
| Loa kêu "bụp", rè | Gắn tụ 470µF + 100nF sát DFPlayer, hàn chắc dây GND |
| Phun sương tự bật lúc khởi động | Thiếu trở 10kΩ kéo Gate xuống GND |
| Phun sương không chạy dù MOSFET mở | Module cần bấm nút: xem mục 3.3 |
| MOSFET nóng | Đang dùng IRF520, đổi sang IRLZ44N |
| Camera báo lỗi 0x105 / 0x20004 | Cáp camera chưa cắm chặt, hoặc chưa chọn PSRAM "OPI PSRAM" trong Arduino IDE |
