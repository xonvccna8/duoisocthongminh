#pragma once
// =====================================================================
//  CẤU HÌNH CHÂN - Hệ thống đuổi sóc thông minh
//  Board: Freenove ESP32-S3-WROOM CAM (module N8R8)
//  Nếu dùng board khác, chỉ cần sửa các chân trong file này.
// =====================================================================

// ---------- Cảm biến ----------
#define PIN_PIR        14   // OUT của PIR (HC-SR501 / AM312)

// I2C dùng chung: DS3231 (RTC), BH1750 (ánh sáng), INA219 (pin)
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    47

// ---------- DFPlayer Mini (loa) ----------
#define PIN_DF_TX      42   // ESP32 TX  -> DFPlayer RX (qua điện trở 1k)
#define PIN_DF_RX      41   // ESP32 RX  <- DFPlayer TX

// ---------- Cơ cấu chấp hành (qua MOSFET) ----------
#define PIN_LED_DRV    1    // Gate MOSFET đèn LED 12V
#define PIN_MIST_DRV   48   // Gate MOSFET module phun sương 5V

// ---------- Thẻ nhớ (có sẵn trên board, chế độ SD_MMC 1-bit) ----------
#define PIN_SD_CLK     39
#define PIN_SD_CMD     38
#define PIN_SD_D0      40

// ---------- Camera OV2640/OV5640 (có sẵn trên board, layout ESP32S3_EYE) ----------
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5
#define CAM_PIN_D7     16   // Y9
#define CAM_PIN_D6     17   // Y8
#define CAM_PIN_D5     18   // Y7
#define CAM_PIN_D4     12   // Y6
#define CAM_PIN_D3     10   // Y5
#define CAM_PIN_D2     8    // Y4
#define CAM_PIN_D1     9    // Y3
#define CAM_PIN_D0     11   // Y2
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK   13

// ---------- Địa chỉ I2C ----------
#define ADDR_BH1750    0x23 // chân ADDR nối GND (hoặc để hở)
#define ADDR_INA219    0x40 // A0, A1 để hở
#define ADDR_DS3231    0x68
#define ADDR_OLED      0x3C // OLED 0,96" SSD1306 (một số màn là 0x3D)

// ---------- Tham số test ----------
#define DF_VOLUME_DEFAULT  20     // 0..30
#define MIST_MAX_ON_MS     10000  // tự tắt phun sương sau 10s (an toàn)
#define DEMO_ACTION_MS     5000   // thời gian xua đuổi trong chế độ demo

#define AP_SSID   "DuoiSoc-Test"
#define AP_PASS   "12345678"
