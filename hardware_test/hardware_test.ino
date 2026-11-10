/*
 * =====================================================================
 *  TEST PHẦN CỨNG - HỆ THỐNG ĐUỔI SÓC THÔNG MINH
 *  Board: Freenove ESP32-S3-WROOM CAM (N8R8)
 *
 *  Arduino IDE:
 *    - Board: "ESP32S3 Dev Module"
 *    - PSRAM: "OPI PSRAM"
 *    - Flash Size: "8MB"
 *    - USB CDC On Boot: "Disabled" (cắm cáp vào cổng "COM"/UART)
 *    - Serial Monitor: 115200 baud, chọn "Newline" hoặc "No line ending"
 *
 *  Thư viện cần cài (Library Manager):
 *    - DFRobotDFPlayerMini  (DFRobot)
 *    - RTClib               (Adafruit)
 *    - BH1750               (Christopher Laws)
 *    - Adafruit INA219      (Adafruit)
 *    - Adafruit SSD1306     (Adafruit, cài kèm Adafruit GFX) - màn OLED 0,96"
 *
 *  Gõ 'h' trong Serial Monitor để xem menu.
 * =====================================================================
 */
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SD_MMC.h>
#include "esp_camera.h"
#include <DFRobotDFPlayerMini.h>
#include <RTClib.h>
#include <BH1750.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;
RTC_DS3231 rtc;
BH1750 lightMeter(ADDR_BH1750);
Adafruit_INA219 ina219(ADDR_INA219);
Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000UL, 100000UL);
WebServer server(80);

bool okRtc = false, okLux = false, okIna = false, okDf = false, okCam = false, okSd = false, okOled = false;
bool ledOn = false, mistOn = false, webOn = false, demoOn = false;
uint8_t dfVolume = DF_VOLUME_DEFAULT;
uint32_t mistOnAt = 0, demoActionUntil = 0;
int photoCount = 0;

// ---------------------------------------------------------------------
//  Tiện ích
// ---------------------------------------------------------------------
const char *okStr(bool ok) { return ok ? "OK  " : "LOI "; }

void setLed(bool on) {
  ledOn = on;
  digitalWrite(PIN_LED_DRV, on ? HIGH : LOW);
  Serial.printf("[LED] %s\n", on ? "BAT" : "TAT");
}

void setMist(bool on) {
  mistOn = on;
  digitalWrite(PIN_MIST_DRV, on ? HIGH : LOW);
  if (on) mistOnAt = millis();
  Serial.printf("[PHUN SUONG] %s\n", on ? "BAT" : "TAT");
}

// ---------------------------------------------------------------------
//  Khởi tạo từng khối
// ---------------------------------------------------------------------
bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0;  c.pin_d1 = CAM_PIN_D1;
  c.pin_d2 = CAM_PIN_D2;  c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4;  c.pin_d5 = CAM_PIN_D5;
  c.pin_d6 = CAM_PIN_D6;  c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_pclk = CAM_PIN_PCLK;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = psramFound() ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;
  c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Khoi tao loi 0x%x\n", err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  Serial.printf("[CAMERA] OK, sensor PID=0x%x\n", s ? s->id.PID : 0);
  return true;
}

bool initSd() {
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) return false;   // true = chế độ 1-bit
  if (SD_MMC.cardType() == CARD_NONE) return false;
  Serial.printf("[SD] OK, dung luong %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
  return true;
}

bool initDfPlayer() {
  dfSerial.begin(9600, SERIAL_8N1, PIN_DF_RX, PIN_DF_TX);
  delay(1000);  // DFPlayer cần ~1s để đọc thẻ nhớ
  if (!dfPlayer.begin(dfSerial, true, true)) return false;
  dfPlayer.volume(dfVolume);
  Serial.printf("[DFPLAYER] OK, so file tren the: %d\n", dfPlayer.readFileCounts());
  return true;
}

void initAll() {
  Serial.println("\n===== KHOI TAO PHAN CUNG =====");
  okRtc = rtc.begin(&Wire);
  okLux = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, ADDR_BH1750, &Wire);
  okIna = ina219.begin(&Wire);
  okOled = oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
  okDf  = initDfPlayer();
  okCam = initCamera();
  okSd  = initSd();
  printStatus();
}

void printStatus() {
  Serial.println("\n----- TRANG THAI -----");
  Serial.printf("  %s DS3231 (RTC)       0x%02X\n", okStr(okRtc), ADDR_DS3231);
  Serial.printf("  %s BH1750 (anh sang)  0x%02X\n", okStr(okLux), ADDR_BH1750);
  Serial.printf("  %s INA219 (pin)       0x%02X\n", okStr(okIna), ADDR_INA219);
  Serial.printf("  %s OLED 0,96\"        0x%02X\n", okStr(okOled), ADDR_OLED);
  Serial.printf("  %s DFPlayer (loa)\n", okStr(okDf));
  Serial.printf("  %s Camera\n", okStr(okCam));
  Serial.printf("  %s The nho SD\n", okStr(okSd));
  Serial.printf("  PSRAM: %s (%u KB)\n", psramFound() ? "co" : "KHONG", ESP.getPsramSize() / 1024);
  Serial.println("----------------------");
}

// ---------------------------------------------------------------------
//  Các bài test
// ---------------------------------------------------------------------
void testI2cScan() {
  Serial.println("\n[I2C] Quet bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      const char *name = "";
      if (addr == ADDR_DS3231) name = "DS3231 (RTC)";
      else if (addr == 0x57)   name = "AT24C32 (EEPROM tren module DS3231)";
      else if (addr == ADDR_BH1750 || addr == 0x5C) name = "BH1750";
      else if (addr >= 0x40 && addr <= 0x4F) name = "INA219";
      else if (addr == 0x3C || addr == 0x3D) name = "OLED SSD1306";
      Serial.printf("  Tim thay 0x%02X  %s\n", addr, name);
      found++;
    }
  }
  Serial.printf("[I2C] Tong: %d thiet bi. Mong doi: 0x23, 0x40, 0x57, 0x68\n", found);
}

void testPir() {
  Serial.println("\n[PIR] Theo doi 15 giay - hay vay tay truoc cam bien...");
  Serial.println("      (HC-SR501 can 30-60s 'lam nong' sau khi cap dien)");
  int last = -1, triggers = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    int v = digitalRead(PIN_PIR);
    if (v != last) {
      Serial.printf("  %5lums  PIR = %s\n", millis() - t0, v ? "HIGH (co chuyen dong)" : "LOW");
      if (v) triggers++;
      last = v;
    }
    delay(20);
  }
  Serial.printf("[PIR] Ket thuc: %d lan kich hoat\n", triggers);
}

void testRtc() {
  if (!okRtc) { Serial.println("[RTC] Chua ket noi"); return; }
  DateTime n = rtc.now();
  Serial.printf("[RTC] %02d/%02d/%04d %02d:%02d:%02d | nhiet do %.2f C | %s\n",
                n.day(), n.month(), n.year(), n.hour(), n.minute(), n.second(),
                rtc.getTemperature(), rtc.lostPower() ? "DA MAT NGUON - go 't' de dat gio" : "gio hop le");
}

void setRtcFromCompileTime() {
  if (!okRtc) { Serial.println("[RTC] Chua ket noi"); return; }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  Serial.println("[RTC] Da dat gio theo thoi diem bien dich");
  testRtc();
}

void testLux() {
  if (!okLux) { Serial.println("[BH1750] Chua ket noi"); return; }
  Serial.println("\n[BH1750] Doc 5 lan - thu che/chieu den vao cam bien");
  for (int i = 0; i < 5; i++) {
    float lux = lightMeter.readLightLevel();
    Serial.printf("  %.1f lux  -> %s\n", lux, lux < 10 ? "TOI (dem)" : lux < 1000 ? "chang vang / trong nha" : "SANG (ngay)");
    delay(500);
  }
}

void testIna() {
  if (!okIna) { Serial.println("[INA219] Chua ket noi"); return; }
  float busV = ina219.getBusVoltage_V();
  float shuntmV = ina219.getShuntVoltage_mV();
  float loadV = busV + shuntmV / 1000.0;
  float mA = ina219.getCurrent_mA();
  float mW = ina219.getPower_mW();
  // Ước lượng % pin cho pin 3S Li-ion (9.0V - 12.6V). Dùng ắc quy chì thì sửa lại.
  float pct = constrain((loadV - 9.0) / (12.6 - 9.0) * 100.0, 0, 100);
  Serial.printf("[INA219] Pin: %.2f V | Dong: %.1f mA | Cong suat: %.0f mW | ~%.0f%%\n", loadV, mA, mW, pct);
}

void testSoundPlay(int track) {
  if (!okDf) { Serial.println("[DFPLAYER] Chua ket noi"); return; }
  dfPlayer.play(track);
  Serial.printf("[DFPLAYER] Phat file %04d.mp3, am luong %d\n", track, dfVolume);
}

void changeVolume(int delta) {
  if (!okDf) { Serial.println("[DFPLAYER] Chua ket noi"); return; }
  dfVolume = constrain(dfVolume + delta, 0, 30);
  dfPlayer.volume(dfVolume);
  Serial.printf("[DFPLAYER] Am luong = %d\n", dfVolume);
}

void testCamera() {
  if (!okCam) { Serial.println("[CAMERA] Chua ket noi"); return; }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { Serial.println("[CAMERA] Chup that bai"); return; }
  Serial.printf("[CAMERA] Chup OK: %ux%u, %u bytes\n", fb->width, fb->height, fb->len);
  if (okSd) {
    char path[32];
    snprintf(path, sizeof(path), "/test_%03d.jpg", photoCount++);
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) {
      f.write(fb->buf, fb->len);
      f.close();
      Serial.printf("[SD] Da luu %s\n", path);
    } else {
      Serial.println("[SD] Khong ghi duoc file");
    }
  }
  esp_camera_fb_return(fb);
}

// ---------------------------------------------------------------------
//  Web xem ảnh camera (kiểm tra góc nhìn, nét)
// ---------------------------------------------------------------------
void handleRoot() {
  server.send(200, "text/html",
    "<html><head><meta name='viewport' content='width=device-width'>"
    "<title>Test camera</title></head><body style='font-family:sans-serif;text-align:center'>"
    "<h3>Camera duoi soc - test</h3>"
    "<img id='i' src='/capture' style='max-width:100%'><br>"
    "<button onclick=\"i.src='/capture?'+Date.now()\" style='font-size:20px;margin:10px'>Chup lai</button>"
    "</body></html>");
}

void handleCapture() {
  camera_fb_t *fb = okCam ? esp_camera_fb_get() : nullptr;
  if (!fb) { server.send(500, "text/plain", "Camera loi"); return; }
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void startWeb() {
  if (webOn) { Serial.printf("[WEB] Dang chay: http://%s\n", WiFi.softAPIP().toString().c_str()); return; }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.begin();
  webOn = true;
  Serial.printf("[WEB] Ket noi WiFi '%s' (mat khau %s) roi mo http://%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}

// ---------------------------------------------------------------------
//  Test tự động & demo
// ---------------------------------------------------------------------
// Màn OLED: sáng toàn bộ -> khung + chữ -> chữ to -> đếm 5..1 -> màn hình chờ
void testOled() {
  Serial.println("\n[OLED] Test man hinh...");
  if (!okOled) okOled = oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
  if (!okOled) {
    Serial.printf("[OLED] LOI: khong thay man o 0x%02X. Kiem tra VCC-3V3, GND, SDA-21, SCL-47\n", ADDR_OLED);
    return;
  }
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  Serial.println("[OLED] 1/4 Sang toan bo man (tim diem chet)");
  oled.fillScreen(SSD1306_WHITE);
  oled.display();
  delay(1500);

  Serial.println("[OLED] 2/4 Khung vien + chu nho");
  oled.clearDisplay();
  oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(6, 6);   oled.print("TEST OLED 0.96\"");
  oled.setCursor(6, 20);  oled.print("128 x 64  SSD1306");
  oled.setCursor(6, 34);  oled.printf("Dia chi I2C 0x%02X", ADDR_OLED);
  oled.setCursor(6, 48);  oled.print("SDA 21  SCL 47");
  oled.display();
  delay(2000);

  Serial.println("[OLED] 3/4 Chu to");
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(4, 8);   oled.print("DUOI SOC");
  oled.setCursor(4, 36);  oled.print("OLED OK!");
  oled.display();
  delay(2000);

  Serial.println("[OLED] 4/4 Dem nguoc + dao mau");
  for (int i = 5; i >= 1; i--) {
    oled.clearDisplay();
    oled.setTextSize(4);
    oled.setCursor(52, 18);
    oled.print(i);
    oled.invertDisplay(i % 2 == 0);
    oled.display();
    delay(600);
  }
  oled.invertDisplay(false);
  oledIdle();
  Serial.println("[OLED] XONG - neu thay du 4 buoc tren man hinh la OLED tot");
}

// Màn hình chờ của sketch test
void oledIdle() {
  if (!okOled) return;
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(0, 4);
  oled.print("TEST P.CUNG");
  oled.setTextSize(1);
  oled.setCursor(0, 30); oled.print("OLED: OK");
  oled.setCursor(0, 42); oled.print("Go 'h' tren Serial");
  oled.setCursor(0, 54); oled.print("de xem menu test");
  oled.display();
}

void testAll() {
  Serial.println("\n######## TEST TU DONG ########");
  testI2cScan();
  testRtc();
  testLux();
  testIna();
  testOled();
  testCamera();
  Serial.println("\n[TEST] LED bat 2s...");
  setLed(true);  delay(2000); setLed(false);
  Serial.println("[TEST] Phun suong bat 2s...");
  setMist(true); delay(2000); setMist(false);
  Serial.println("[TEST] Phat am thanh file 0001...");
  testSoundPlay(1); delay(3000);
  if (okDf) dfPlayer.stop();
  testIna();  // đo lại sau khi tải hoạt động
  printStatus();
  Serial.println("######## XONG - go '2' de test PIR ########");
}

// Demo: PIR kích hoạt -> chụp ảnh -> bật LED + loa + phun sương trong DEMO_ACTION_MS
void demoLoop() {
  uint32_t now = millis();
  if (demoActionUntil && now > demoActionUntil) {
    demoActionUntil = 0;
    setLed(false);
    setMist(false);
    if (okDf) dfPlayer.stop();
    Serial.println("[DEMO] Ket thuc xua duoi, cho chuyen dong tiep theo...");
  }
  if (!demoActionUntil && digitalRead(PIN_PIR)) {
    Serial.println("\n[DEMO] PIR phat hien chuyen dong!");
    testCamera();
    setLed(true);
    setMist(true);
    if (okDf) dfPlayer.play(random(1, max(2, dfPlayer.readFileCounts() + 1)));  // file ngẫu nhiên
    demoActionUntil = now + DEMO_ACTION_MS;
  }
}

void printHelp() {
  Serial.println(F("\n=========== MENU TEST PHAN CUNG ==========="));
  Serial.println(F("  h  - Menu nay          i  - Khoi tao lai tat ca"));
  Serial.println(F("  0  - Trang thai        a  - TEST TU DONG tat ca"));
  Serial.println(F("  1  - Quet I2C"));
  Serial.println(F("  2  - Test PIR (15s)"));
  Serial.println(F("  3  - Doc RTC           t  - Dat gio RTC"));
  Serial.println(F("  4  - Doc anh sang BH1750"));
  Serial.println(F("  5  - Doc pin INA219"));
  Serial.println(F("  6  - Phat am thanh     n  - Bai tiep   s - Dung"));
  Serial.println(F("  +  - Tang am luong     -  - Giam am luong"));
  Serial.println(F("  7  - Bat/tat LED"));
  Serial.println(F("  8  - Bat/tat phun suong (tu tat sau 10s)"));
  Serial.println(F("  9  - Chup anh (luu SD neu co)"));
  Serial.println(F("  o  - Test man hinh OLED"));
  Serial.println(F("  w  - Bat WiFi xem camera tren dien thoai"));
  Serial.println(F("  d  - Bat/tat DEMO (PIR -> xua duoi)"));
  Serial.println(F("============================================"));
}

// ---------------------------------------------------------------------
void setup() {
  // Đặt chân MOSFET về LOW sớm nhất để tải không tự bật khi khởi động
  pinMode(PIN_LED_DRV, OUTPUT);  digitalWrite(PIN_LED_DRV, LOW);
  pinMode(PIN_MIST_DRV, OUTPUT); digitalWrite(PIN_MIST_DRV, LOW);
  pinMode(PIN_PIR, INPUT);

  Serial.begin(115200);
  delay(1500);
  Serial.println("\n*** HE THONG DUOI SOC THONG MINH - TEST PHAN CUNG ***");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  initAll();
  if (okOled) testOled();   // có màn OLED thì tự test ngay khi khởi động
  printHelp();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'h': printHelp(); break;
      case 'i': initAll(); break;
      case '0': printStatus(); break;
      case 'a': testAll(); break;
      case '1': testI2cScan(); break;
      case '2': testPir(); break;
      case '3': testRtc(); break;
      case 't': setRtcFromCompileTime(); break;
      case '4': testLux(); break;
      case '5': testIna(); break;
      case '6': testSoundPlay(1); break;
      case 'n': if (okDf) { dfPlayer.next(); Serial.println("[DFPLAYER] Bai tiep"); } break;
      case 's': if (okDf) { dfPlayer.stop(); Serial.println("[DFPLAYER] Dung"); } break;
      case '+': changeVolume(+2); break;
      case '-': changeVolume(-2); break;
      case '7': setLed(!ledOn); break;
      case '8': setMist(!mistOn); break;
      case '9': testCamera(); break;
      case 'o': testOled(); break;
      case 'w': startWeb(); break;
      case 'd':
        demoOn = !demoOn;
        demoActionUntil = 0;
        setLed(false); setMist(false);
        Serial.printf("[DEMO] %s\n", demoOn ? "BAT - vay tay truoc PIR de thu" : "TAT");
        break;
      case '\r': case '\n': case ' ': break;
      default: Serial.printf("Lenh '%c' khong hop le, go 'h'\n", c);
    }
  }

  // An toàn: phun sương không chạy quá MIST_MAX_ON_MS (trừ khi đang demo xua đuổi)
  if (mistOn && !demoActionUntil && millis() - mistOnAt > MIST_MAX_ON_MS) {
    Serial.println("[AN TOAN] Tu tat phun suong");
    setMist(false);
  }

  if (demoOn) demoLoop();
  if (webOn) server.handleClient();
  delay(5);
}
