// =====================================================================
//  KIỂM TRA CAMERA - tìm nguyên nhân lỗi "Detected camera not supported" (0x106)
//  Board ESP32-S3 N16R8 CAM (OV2640 / OV3660 / OV5640). Kết quả in ra cổng USB-UART (COM), 115200.
//    1. Mức điện chân I2C camera (GPIO4 SIOD, GPIO5 SIOC) và I2C cảm biến (GPIO21, GPIO47)
//    2. Dò mọi địa chỉ I2C trên đường cảm biến (21/47)
//    3. Bật xung XCLK cho camera rồi dò mọi địa chỉ trên đường camera (4/5), đọc mã chip
//    4. Khởi động camera thật và chụp thử 1 ảnh
//  Nạp bằng FQBN mặc định (không CDCOnBoot) để Serial ra cổng UART.
// =====================================================================
#include <Wire.h>
#include "esp_camera.h"
#include "../duoi_soc_web/config.h"

void pinLevels() {
  const int pins[] = {CAM_PIN_SIOD, CAM_PIN_SIOC, PIN_I2C_SDA, PIN_I2C_SCL};
  const char *names[] = {"GPIO4  SIOD camera", "GPIO5  SIOC camera", "GPIO21 SDA cam bien", "GPIO47 SCL cam bien"};
  Serial.println("[1] Muc dien khi nghi (dung: 1 = cao; 0 = bi keo xuong GND / cham chan)");
  for (int i = 0; i < 4; i++) {
    pinMode(pins[i], INPUT_PULLUP);
    delay(5);
    int up = digitalRead(pins[i]);
    pinMode(pins[i], INPUT_PULLDOWN);
    delay(5);
    int down = digitalRead(pins[i]);
    pinMode(pins[i], INPUT);
    Serial.printf("    %-20s keo len=%d keo xuong=%d  -> %s\n", names[i], up, down,
                  up && down ? "co dien tro keo len (co module noi vao)" : up ? "trong (khong noi gi)" : "BI KEO XUONG GND!");
  }
}

int scan(TwoWire &w, const char *label) {
  int n = 0;
  Serial.printf("[%s] Do dia chi I2C:", label);
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; }
  }
  Serial.println(n ? "" : " (khong co thiet bi nao)");
  return n;
}

int readReg16(uint8_t addr, uint16_t reg) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg >> 8); Wire1.write(reg & 0xFF);
  if (Wire1.endTransmission(false) != 0) return -1;
  if (Wire1.requestFrom(addr, (uint8_t)1) != 1) return -2;
  return Wire1.read();
}

int readReg8(uint8_t addr, uint8_t reg) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return -1;
  if (Wire1.requestFrom(addr, (uint8_t)1) != 1) return -2;
  return Wire1.read();
}

void cameraBus() {
  // Camera OV3660/OV5640 chi tra loi I2C khi co xung XCLK
  ledcAttach(CAM_PIN_XCLK, 20000000, 1);
  ledcWrite(CAM_PIN_XCLK, 1);
  delay(50);
  Wire1.begin(CAM_PIN_SIOD, CAM_PIN_SIOC, 100000);
  scan(Wire1, "3 camera 4/5");
  for (uint8_t a : {0x3C, 0x30, 0x21, 0x68}) {
    Wire1.beginTransmission(a);
    if (Wire1.endTransmission() != 0) continue;
    if (a == 0x3C) {
      int h = readReg16(a, 0x300A), l = readReg16(a, 0x300B);
      Serial.printf("    0x3C: ma chip 0x300A/B = %d / %d  (OV3660 = 0x36/0x60, OV5640 = 0x56/0x40, am = khong doc duoc)\n", h, l);
      if (h < 0) Serial.println("    -> 0x3C nhan dia chi nhung KHONG cho doc: giong man OLED SSD1306, KHONG phai camera");
    } else if (a == 0x30) {
      Wire1.beginTransmission(a); Wire1.write(0xFF); Wire1.write(0x01); Wire1.endTransmission();
      Serial.printf("    0x30: ma chip PID/VER = 0x%02X / 0x%02X  (OV2640 = 0x26/0x42)\n", readReg8(a, 0x0A), readReg8(a, 0x0B));
    } else if (a == 0x68) {
      Serial.println("    0x68: dia chi cua dong ho RTC DS3231 -> co module RTC noi nham vao chan camera?");
    } else {
      Serial.printf("    0x%02X: ma chip = 0x%02X 0x%02X\n", a, readReg8(a, 0x0A), readReg8(a, 0x0B));
    }
  }
  Wire1.end();
  ledcDetach(CAM_PIN_XCLK);
}

void cameraInit() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0;  c.pin_d1 = CAM_PIN_D1;  c.pin_d2 = CAM_PIN_D2;  c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4;  c.pin_d5 = CAM_PIN_D5;  c.pin_d6 = CAM_PIN_D6;  c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK;  c.pin_pclk = CAM_PIN_PCLK;  c.pin_vsync = CAM_PIN_VSYNC;  c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn = CAM_PIN_PWDN;  c.pin_reset = CAM_PIN_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;  c.frame_size = FRAMESIZE_VGA;  c.jpeg_quality = 12;
  c.fb_count = 1;  c.fb_location = CAMERA_FB_IN_PSRAM;  c.grab_mode = CAMERA_GRAB_LATEST;
  esp_err_t e = esp_camera_init(&c);
  if (e != ESP_OK) { Serial.printf("[4] Khoi dong camera: LOI 0x%x\n", e); return; }
  sensor_t *s = esp_camera_sensor_get();
  Serial.printf("[4] Khoi dong camera: OK, cam bien PID=0x%x\n", s ? s->id.PID : 0);
  camera_fb_t *fb = esp_camera_fb_get();
  Serial.printf("    Chup thu: %s (%u byte, %ux%u)\n", fb ? "OK" : "LOI", fb ? fb->len : 0, fb ? fb->width : 0, fb ? fb->height : 0);
  if (fb) esp_camera_fb_return(fb);
  esp_camera_deinit();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n===== KIEM TRA CAMERA =====");
  Serial.printf("PSRAM: %s (%u byte)\n", psramFound() ? "co" : "KHONG", ESP.getPsramSize());
  pinLevels();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  scan(Wire, "2 cam bien 21/47");
  Wire.end();
  cameraBus();
  cameraInit();
  Serial.println("===== XONG =====");
}

void loop() {}
