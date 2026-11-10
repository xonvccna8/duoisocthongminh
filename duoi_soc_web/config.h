#pragma once
// =====================================================================
//  CẤU HÌNH - Hệ thống đuổi sóc thông minh (bản có web)
//  Board: Freenove ESP32-S3-WROOM CAM (module N8R8)
//  Sơ đồ chân giống hệt hardware_test/config.h
// =====================================================================

// ---------- WiFi ----------
// WiFi nhà vườn: nên nhập trên web thiết bị (nút "Internet"), lưu trong board.
// Hai dòng dưới chỉ là mặc định khi board chưa lưu WiFi nào.
#define WIFI_SSID      ""
#define WIFI_PASS      ""
// Web Vercel nhận trạng thái + ảnh từ thiết bị để xem từ xa qua Internet ("" = tắt)
#define CLOUD_URL      "https://duoi-soc-thong-minh.vercel.app"
#define TZ_OFFSET_SEC  (7 * 3600)   // giờ Việt Nam (UTC+7), dùng khi lấy giờ qua Internet
// WiFi riêng do thiết bị phát ra - luôn bật. Vào WiFi này rồi mở http://192.168.4.1
#define AP_SSID        "DuoiSoc"
#define AP_PASS        "12345678"     // tối thiểu 8 ký tự
#define MDNS_NAME      "duoisoc"      // http://duoisoc.local

// ---------- Cảm biến ----------
#define PIN_PIR        14
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    47

// ---------- Loa: mạch khuếch đại I2S MAX98357A ----------
// File âm thanh: thẻ nhớ của board, /sound/1.wav, /sound/2.wav... (WAV PCM 16-bit)
#define PIN_I2S_BCLK   42   // -> BCLK
#define PIN_I2S_LRC    41   // -> LRC
#define PIN_I2S_DOUT   2    // -> DIN  (GPIO2 nối cả đèn LED trên board: đèn nháy khi phát tiếng là bình thường)

// ---------- MOSFET ----------
#define PIN_LED_DRV    1
#define PIN_MIST_DRV   48

// ---------- Thẻ nhớ SD_MMC 1-bit ----------
#define PIN_SD_CLK     39
#define PIN_SD_CMD     38
#define PIN_SD_D0      40

// ---------- Camera (layout ESP32S3_EYE) ----------
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5
#define CAM_PIN_D7     16
#define CAM_PIN_D6     17
#define CAM_PIN_D5     18
#define CAM_PIN_D4     12
#define CAM_PIN_D3     10
#define CAM_PIN_D2     8
#define CAM_PIN_D1     9
#define CAM_PIN_D0     11
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK   13

// ---------- I2C ----------
#define ADDR_BH1750    0x23
#define ADDR_INA219    0x40
#define ADDR_OLED      0x3C   // OLED 0,96" SSD1306 (một số màn là 0x3D)

// ---------- Ngưỡng ----------
#define NIGHT_LUX        15.0   // dưới mức này coi là trời tối
#define BAT_EMPTY_V      9.0    // pin 3S Li-ion: 9.0V = 0%
#define BAT_FULL_V       12.6   //                12.6V = 100%  (ắc quy chì: 11.8 / 12.8)
#define MIST_MAX_ON_MS   15000  // phun sương thủ công tự tắt sau 15s
#define MAX_EVENTS       40     // số sự kiện giữ trong RAM để hiển thị trên web
