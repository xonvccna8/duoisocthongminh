/*
 * =====================================================================
 *  HỆ THỐNG ĐUỔI SÓC THÔNG MINH - FIRMWARE + WEB GIÁM SÁT
 *  Board: Freenove ESP32-S3-WROOM CAM (N8R8)
 *
 *  Arduino IDE:
 *    - Board: "ESP32S3 Dev Module", PSRAM: "OPI PSRAM", Flash Size: "8MB"
 *    - Partition Scheme: để MẶC ĐỊNH là được (web + ảnh đã nén, firmware ~1,27MB vừa 1,25MB APP).
 *      Thêm code mà báo "Sketch too big" thì chọn "8M with spiffs (3MB APP/1.5MB SPIFFS)".
 *    - USB CDC On Boot: "Disabled" (cắm cáp vào cổng COM/UART)
 *
 *  Thư viện: RTClib, BH1750 (Christopher Laws), Adafruit INA219,
 *             Adafruit SSD1306 (+ Adafruit GFX) cho màn hình OLED 0,96"
 *
 *  Sử dụng:
 *    1. WiFi nhà vườn: mở web thiết bị -> "Internet" -> nhập tên + mật khẩu WiFi (lưu trong board).
 *       Có Internet thì thiết bị tự gửi trạng thái + ảnh sóc lên web Vercel (xem cloud.ino).
 *    2. Nạp code, mở Serial Monitor 115200 để xem địa chỉ IP.
 *    3. Điện thoại vào WiFi "DuoiSoc" (mk 12345678) -> mở http://192.168.4.1
 *       hoặc trong mạng nhà vườn -> mở http://<IP> hoặc http://duoisoc.local
 *
 *  Giao diện web nằm trong web_page.h, được sinh tự động từ web/index.html
 *  bằng tools/build_web.ps1. Sửa giao diện thì sửa index.html rồi chạy lại script.
 * =====================================================================
 */
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <FS.h>
#include <SD_MMC.h>
#include "esp_camera.h"
#include <ESP_I2S.h>
#include <esp_http_server.h>   // video MJPEG cổng 81 (stream.ino)
#include <RTClib.h>
#include <BH1750.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "am_thanh.h"     // tiếng mèo, chó, diều hâu, cú có sẵn (tools/tao_am_thanh_soc.py)
#include "web_page.h"
#include "img_assets.h"

I2SClass i2s;   // loa: mạch khuếch đại MAX98357A
RTC_DS3231 rtc;
BH1750 lightMeter(ADDR_BH1750);
Adafruit_INA219 ina219(ADDR_INA219);
// OLED: truyền nhanh 400kHz khi vẽ, trả về 100kHz cho các module I2C khác
Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000UL, 100000UL);
WebServer server(80);
Preferences prefs;

bool okRtc = false, okLux = false, okIna = false, okSound = false, okCam = false, okSd = false, okOled = false;
int sndTracks = 0;   // số file /sound/1.wav, 2.wav... trên thẻ nhớ

// ---------------------------------------------------------------------
//  Cài đặt (lưu trong flash, giữ khi mất điện)
// ---------------------------------------------------------------------
struct Settings {
  bool armed = true;
  bool autoMode = true;
  uint8_t threshold = 60;      // điểm AI tối thiểu để coi là sóc
  uint16_t repelSec = 8;       // thời gian mỗi lượt xua đuổi
  uint16_t cooldownSec = 5;    // nghỉ giữa 2 lượt xua (và sau lượt cuối, trước khi canh gác lại)
  uint8_t repelRounds = 2;     // mỗi lần thấy sóc xua mấy lượt
  bool useSound = true, useLed = true, useMist = true;
  bool nightOnly = false;
  bool savePhoto = true;
  uint8_t volume = 30;   // to nhất (0-30)
} cfg;

void loadSettings() {
  prefs.begin("duoisoc", false);
  cfg.armed       = prefs.getBool("armed", cfg.armed);
  cfg.autoMode    = prefs.getBool("auto", cfg.autoMode);
  cfg.threshold   = prefs.getUChar("thr", cfg.threshold);
  cfg.repelSec    = prefs.getUShort("rep", cfg.repelSec);
  cfg.cooldownSec = prefs.getUShort("cool", cfg.cooldownSec);
  cfg.repelRounds = prefs.getUChar("rnd", cfg.repelRounds);
  cfg.useSound    = prefs.getBool("snd", cfg.useSound);
  cfg.useLed      = prefs.getBool("led", cfg.useLed);
  cfg.useMist     = prefs.getBool("mist", cfg.useMist);
  cfg.nightOnly   = prefs.getBool("night", cfg.nightOnly);
  cfg.savePhoto   = prefs.getBool("photo", cfg.savePhoto);
  cfg.volume      = prefs.getUChar("vol2", cfg.volume);
}

void saveSettings() {
  prefs.putBool("armed", cfg.armed);
  prefs.putBool("auto", cfg.autoMode);
  prefs.putUChar("thr", cfg.threshold);
  prefs.putUShort("rep", cfg.repelSec);
  prefs.putUShort("cool", cfg.cooldownSec);
  prefs.putUChar("rnd", cfg.repelRounds);
  prefs.putBool("snd", cfg.useSound);
  prefs.putBool("led", cfg.useLed);
  prefs.putBool("mist", cfg.useMist);
  prefs.putBool("night", cfg.nightOnly);
  prefs.putBool("photo", cfg.savePhoto);
  prefs.putUChar("vol2", cfg.volume);
}

// ---------------------------------------------------------------------
//  Đồng hồ: lấy từ DS3231, hoặc từ trình duyệt nếu không có RTC
//  (thời gian lưu dạng "giờ địa phương" tính bằng giây từ 1970)
// ---------------------------------------------------------------------
uint32_t clockBaseTs = 0, clockBaseMs = 0;

uint32_t nowTs() {
  if (clockBaseTs) return clockBaseTs + (millis() - clockBaseMs) / 1000;
  return millis() / 1000;
}
bool clockValid() { return nowTs() > 1600000000UL; }
int hourNow() { return clockValid() ? (nowTs() % 86400UL) / 3600 : (millis() / 3600000UL) % 24; }

void syncClockFromRtc() {
  if (!okRtc || rtc.lostPower()) return;
  clockBaseTs = rtc.now().unixtime();
  clockBaseMs = millis();
}

// ---------------------------------------------------------------------
//  Cảm biến
// ---------------------------------------------------------------------
struct Sensors {
  bool pir = false;
  float lux = NAN, tempC = NAN, batV = NAN, batmA = NAN;
  int batPct = -1;
  bool night = false;
} sn;

void readSensors() {
  if (okLux) {
    float l = lightMeter.readLightLevel();
    if (l >= 0) sn.lux = l;
  }
  if (okIna) {
    sn.batV = ina219.getBusVoltage_V() + ina219.getShuntVoltage_mV() / 1000.0;
    sn.batmA = ina219.getCurrent_mA();
    sn.batPct = constrain((int)((sn.batV - BAT_EMPTY_V) / (BAT_FULL_V - BAT_EMPTY_V) * 100), 0, 100);
  }
  if (okRtc) sn.tempC = rtc.getTemperature();
  if (!isnan(sn.lux)) sn.night = sn.lux < NIGHT_LUX;
  else if (clockValid()) { int h = hourNow(); sn.night = (h < 6 || h >= 18); }
}

// ---------------------------------------------------------------------
//  Nhật ký sự kiện
// ---------------------------------------------------------------------
enum { EV_MOTION, EV_SQUIRREL, EV_REPEL, EV_REPEL_END, EV_MANUAL, EV_SYSTEM };
const char *EV_NAMES[] = {"motion", "squirrel", "repel", "repel_end", "manual", "system"};

struct Event {
  uint32_t id, ts;
  uint8_t type, score;
  int32_t photo;   // ảnh trên thẻ nhớ 0-49999, ảnh máy chủ giữ hộ từ 50000
  char msg[64];
};
Event events[MAX_EVENTS];
uint8_t evHead = 0, evCount = 0;
uint32_t evNextId = 1, lastEventId = 0;

void logEvent(uint8_t type, uint8_t score, int photo, const char *msg) {
  Event &e = events[evHead];
  e.id = evNextId++;
  e.ts = nowTs();
  e.type = type;
  e.score = score;
  e.photo = photo;
  strlcpy(e.msg, msg, sizeof(e.msg));
  evHead = (evHead + 1) % MAX_EVENTS;
  if (evCount < MAX_EVENTS) evCount++;
  lastEventId = e.id;
  Serial.printf("[SU KIEN] %-9s diem=%3u anh=%d  %s\n", EV_NAMES[type], score, photo, msg);
  if (okSd) {
    File f = SD_MMC.open("/events/log.csv", FILE_APPEND);
    if (f) {
      f.printf("%lu,%s,%u,%d,%s\n", (unsigned long)e.ts, EV_NAMES[type], score, photo, msg);
      f.close();
    }
  }
}

// ---------------------------------------------------------------------
//  Thống kê trong ngày
// ---------------------------------------------------------------------
uint16_t hourly[24];
uint16_t todayMotion = 0, todaySquirrel = 0, todayRepel = 0;
uint32_t statDay = 0, lastDetectTs = 0;
uint8_t lastScore = 0;

void checkDayRollover() {
  if (!clockValid()) return;
  uint32_t d = nowTs() / 86400UL;
  if (statDay == 0) { statDay = d; return; }
  if (d != statDay) {
    statDay = d;
    memset(hourly, 0, sizeof(hourly));
    todayMotion = todaySquirrel = todayRepel = 0;
    logEvent(EV_SYSTEM, 0, -1, "Sang ngày mới - đặt lại thống kê");
  }
}

// ---------------------------------------------------------------------
//  Camera & thẻ nhớ
// ---------------------------------------------------------------------
esp_err_t camErr = ESP_OK;   // mã lỗi khởi động camera, hiện trên web để dễ tìm nguyên nhân

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
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
  c.jpeg_quality = 10;   // nhỏ hơn = nét hơn (ảnh VGA ~15 KB)
  c.fb_count = psramFound() ? 3 : 1;   // 3 khung: video, gửi lên Internet và AI lấy ảnh cùng lúc không phải chờ
  c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  camErr = esp_camera_init(&c);
  if (camErr != ESP_OK) {                 // thử lại với xung nhịp thấp hơn (cáp dài / nguồn yếu)
    Serial.printf("[CAMERA] Loi 0x%x, thu lai o 10MHz\n", camErr);
    esp_camera_deinit();
    delay(200);
    c.xclk_freq_hz = 10000000;
    camErr = esp_camera_init(&c);
  }
  if (camErr != ESP_OK) {
    Serial.printf("[CAMERA] Loi 0x%x\n", camErr);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {   // camera OV3660 lắp ngược trên board: lật dọc (giống ví dụ CameraWebServer)
    s->set_vflip(s, 1);
    // Màu tự nhiên, rõ nét hơn: ví dụ Espressif để độ sáng +1, độ bão hòa -2 làm ảnh bạc màu, mờ như sương
    s->set_brightness(s, 0);
    s->set_saturation(s, 0);
    s->set_contrast(s, 1);
    s->set_sharpness(s, 1);
    s->set_lenc(s, 1);   // bù tối 4 góc ống kính
  }
  return true;
}

bool initSd() {
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) return false;
  if (SD_MMC.cardType() == CARD_NONE) return false;
  if (!SD_MMC.exists("/events")) SD_MMC.mkdir("/events");
  return true;
}

int savePhoto(camera_fb_t *fb) {
  int id = prefs.getInt("photoId", 0);
  char path[32];
  snprintf(path, sizeof(path), "/events/%05d.jpg", id);
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return -1;
  f.write(fb->buf, fb->len);
  f.close();
  prefs.putInt("photoId", (id + 1) % 100000);
  return id;
}

uint32_t lastMotionMs = 0;

// ---------------------------------------------------------------------
//  Xua đuổi
// ---------------------------------------------------------------------
enum State : uint8_t { ST_IDLE, ST_REPEL, ST_COOLDOWN };
State state = ST_IDLE;
uint32_t repelEndMs = 0, cooldownEndMs = 0, mistManualAt = 0;
uint8_t repelRound = 0, repelRoundsLeft = 0;   // lượt xua đang chạy / số lượt còn lại của lần thấy sóc này
bool ledManual = false, mistManual = false, soundOn = false;
bool ledOut = false, mistOut = false;

// ---------------------------------------------------------------------
//  Âm thanh qua MAX98357A (I2S):
//   - Có thẻ nhớ: phát file /sound/1.wav, /sound/2.wav... (PCM 16-bit, mono hoặc stereo, mọi tần số mẫu)
//   - Không có: 10 tiếng kẻ thù của sóc có sẵn trong firmware (am_thanh.h: mèo, chó, diều hâu, cú)
// ---------------------------------------------------------------------
File sndFile;
uint32_t sndLeft = 0, sndRate = 0;   // byte âm thanh còn lại, tần số mẫu I2S đang chạy
uint8_t sndCh = 1;

int bPlay = -1, bNext = -1;          // tiếng có sẵn đang phát / phát tiếp sau khoảng nghỉ
uint32_t bPos = 0, bGapUntil = 0;
// Ghi nhớ tiếng nào sóc sợ: sau lượt xua tự động, 10 phút không thấy sóc quay lại thì tính tiếng mở đầu
// lượt đó là "sóc bỏ chạy". Lượt sau ưu tiên tiếng có tỉ lệ bỏ chạy cao và không lặp lại tiếng lượt trước.
uint16_t sndTries[BUILTIN_COUNT], sndFled[BUILTIN_COUNT];
int leadSound = -1, lastLead = -1;   // tiếng mở đầu lượt xua đang chờ kết quả / của lượt trước
uint32_t leadAt = 0;
const uint32_t SND_RETURN_MS = 10UL * 60 * 1000;

bool initSound() {
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  sndRate = 16000;
  return i2s.begin(I2S_MODE_STD, sndRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

int countTracks() {
  int n = 0;
  char path[24];
  while (n < 99) {
    snprintf(path, sizeof(path), "/sound/%d.wav", n + 1);
    if (!SD_MMC.exists(path)) break;
    n++;
  }
  return n;
}

uint32_t le32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

void stopSound() {
  if (sndFile) sndFile.close();
  sndLeft = 0;
  bPlay = bNext = -1;
  soundOn = false;
}

bool playTrack(int n) {
  stopSound();
  if (!okSound || !okSd) return false;
  char path[24];
  snprintf(path, sizeof(path), "/sound/%d.wav", n);
  File f = SD_MMC.open(path);
  if (!f) return false;
  uint8_t h[16];
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { f.close(); return false; }
  uint16_t fmt = 0, ch = 0, bits = 0;
  uint32_t rate = 0;
  while (f.read(h, 8) == 8) {   // duyệt các khối "fmt " / "data" / khác
    uint32_t len = le32(h + 4), next = f.position() + len + (len & 1);
    if (!memcmp(h, "fmt ", 4) && len >= 16 && f.read(h, 16) == 16) {
      fmt = h[0] | h[1] << 8; ch = h[2] | h[3] << 8; rate = le32(h + 4); bits = h[14] | h[15] << 8;
    } else if (!memcmp(h, "data", 4)) {
      if (fmt != 1 || bits != 16 || ch < 1 || ch > 2 || !rate) break;   // chỉ nhận PCM 16-bit
      if (rate != sndRate && i2s.configureTX(rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) sndRate = rate;
      sndFile = f; sndLeft = len; sndCh = ch;
      soundOn = true;
      return true;
    }
    f.seek(next);
  }
  f.close();
  Serial.printf("[LOA] %s khong phai WAV PCM 16-bit\n", path);
  return false;
}

bool isCat(int i) { return !strncmp(BUILTIN_SOUNDS[i].name, "Mèo", 4); }

void loadSoundStats() {
  if (prefs.getBytes("sndT3", sndTries, sizeof(sndTries)) != sizeof(sndTries) ||
      prefs.getBytes("sndF3", sndFled, sizeof(sndFled)) != sizeof(sndFled)) {
    memset(sndTries, 0, sizeof(sndTries));   // lần đầu hoặc bộ tiếng đã đổi
    memset(sndFled, 0, sizeof(sndFled));
  }
}

void soundResult(bool fled) {   // kết quả lượt xua vừa rồi: sóc bỏ chạy hay quay lại
  if (leadSound < 0) return;
  sndTries[leadSound]++;
  if (fled) sndFled[leadSound]++;
  prefs.putBytes("sndT3", sndTries, sizeof(sndTries));
  prefs.putBytes("sndF3", sndFled, sizeof(sndFled));
  Serial.printf("[LOA] %s: %s (%u/%u lan soc bo chay)\n", BUILTIN_SOUNDS[leadSound].name,
                fled ? "soc bo chay" : "soc quay lai", sndFled[leadSound], sndTries[leadSound]);
  leadSound = -1;
}

// Chọn tiếng mở đầu lượt xua: lần đầu là tiếng mèo; sau đó ưu tiên tiếng sóc sợ, không lặp tiếng lượt trước
int pickLead() {
  uint32_t total = 0;
  for (int i = 0; i < BUILTIN_COUNT; i++) total += sndTries[i];
  float w[BUILTIN_COUNT], sum = 0;
  for (int i = 0; i < BUILTIN_COUNT; i++) {
    w[i] = 0;
    if (i == lastLead || (total == 0 && !isCat(i))) continue;
    float p = (sndFled[i] + (isCat(i) ? 1.5f : 1.0f)) / (sndTries[i] + 2.0f);   // tỉ lệ bỏ chạy, chưa có số liệu thì ưu tiên mèo
    w[i] = p * p;
    sum += w[i];
  }
  float r = random(0, 1000000) / 1e6f * sum;
  int pick = 0;
  for (int i = 0; i < BUILTIN_COUNT; i++) {
    if (w[i] <= 0) continue;
    pick = i;
    if ((r -= w[i]) <= 0) break;
  }
  return pick;
}

bool playBuiltin(int i) {
  stopSound();
  if (!okSound || i < 0 || i >= BUILTIN_COUNT) return false;
  if (sndRate != 16000 && i2s.configureTX(16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) sndRate = 16000;
  bPlay = i;
  bPos = 0;
  soundOn = true;
  return true;
}

int16_t ulaw(uint8_t u) {   // giải mã G.711 mu-law
  u = ~u;
  int16_t v = ((((u & 0x0F) << 3) + 0x84) << ((u >> 4) & 7)) - 0x84;
  return (u & 0x80) ? -v : v;
}

void onSoundEnd();

// Phát ~16ms tiếng có sẵn; giữa 2 tiếng nghỉ ngẫu nhiên 0,3-1,2 giây (phát im lặng) cho tự nhiên
void builtinLoop() {
  int16_t out[512];
  if (bPlay < 0) {
    memset(out, 0, sizeof(out));
    i2s.write((uint8_t *)out, sizeof(out));
    if ((int32_t)(millis() - bGapUntil) >= 0) { int nx = bNext; bNext = -1; playBuiltin(nx); }
    return;
  }
  const BuiltinSound &b = BUILTIN_SOUNDS[bPlay];
  size_t n = min<uint32_t>(256, b.len - bPos);
  int32_t gain = (int32_t)cfg.volume * cfg.volume * 256 / 900;
  for (size_t i = 0; i < n; i++) out[2 * i] = out[2 * i + 1] = (int32_t)ulaw(b.data[bPos + i]) * gain >> 8;
  i2s.write((uint8_t *)out, n * 4);
  bPos += n;
  if (bPos >= b.len) {
    int prev = bPlay;
    stopSound();
    if (state == ST_REPEL && cfg.useSound) {   // còn đang xua: nghỉ chút rồi phát tiếng khác
      do bNext = random(0, BUILTIN_COUNT); while (BUILTIN_COUNT > 1 && bNext == prev);
      bGapUntil = millis() + random(300, 1200);
      soundOn = true;
    }
  }
}

// Gọi liên tục trong loop(): mỗi lần đẩy ~16ms âm thanh sang I2S
void soundLoop() {
  if (bPlay >= 0 || bNext >= 0) { builtinLoop(); return; }
  if (!sndFile) return;
  int16_t in[256], out[512];
  size_t got = sndFile.read((uint8_t *)in, min<uint32_t>(sizeof(in), sndLeft)) & ~1u;
  if (!got) { stopSound(); onSoundEnd(); return; }
  sndLeft -= got;
  int32_t gain = (int32_t)cfg.volume * cfg.volume * 256 / 900;   // âm lượng 0-30, tăng theo cảm nhận tai
  size_t n = got / 2, o = 0;
  for (size_t i = 0; i < n; i++) {
    int16_t v = (int32_t)in[i] * gain >> 8;
    out[o++] = v;
    if (sndCh == 1) out[o++] = v;   // mono: phát cả 2 kênh
  }
  i2s.write((uint8_t *)out, o * 2);
}

void playRandom() {
  if (sndTracks < 1) { playBuiltin(random(0, BUILTIN_COUNT)); return; }
  playTrack(random(1, sndTracks + 1));  // phát ngẫu nhiên để sóc không quen
}

// Phát hết 1 file: nếu còn đang xua thì phát tiếp file khác
void onSoundEnd() {
  if (state == ST_REPEL && cfg.useSound) playRandom();
}

// Lượt xua tiếp theo của cùng một lần thấy sóc (nghỉ cfg.cooldownSec giữa 2 lượt), tiếng khác lượt trước
void nextRepelRound() {
  state = ST_REPEL;
  repelEndMs = millis() + cfg.repelSec * 1000UL;
  repelRound++;
  repelRoundsLeft--;
  if (cfg.useSound) playRandom();
  char m[64];
  snprintf(m, sizeof(m), "Xua đuổi lượt %u/%u", repelRound, repelRound + repelRoundsLeft);
  logEvent(EV_REPEL, 0, -1, m);
}

void startRepel(const char *src) {
  if (state == ST_REPEL) return;
  state = ST_REPEL;
  repelEndMs = millis() + cfg.repelSec * 1000UL;
  repelRound = 1;
  repelRoundsLeft = cfg.repelRounds > 1 ? cfg.repelRounds - 1 : 0;
  todayRepel++;
  if (cfg.useSound && sndTracks < 1) {   // tiếng có sẵn: chọn tiếng mở đầu theo trí nhớ, mỗi lượt một tiếng khác
    int lead = pickLead();
    playBuiltin(lead);
    lastLead = lead;
    if (!strcmp(src, "tự động")) { leadSound = lead; leadAt = millis(); }
  } else if (cfg.useSound) playRandom();
  char m[64];
  snprintf(m, sizeof(m), "Bắt đầu xua đuổi (%s)", src);
  logEvent(EV_REPEL, 0, -1, m);
}

void stopRepel(const char *msg) {
  if (state != ST_REPEL) return;
  state = ST_COOLDOWN;
  cooldownEndMs = millis() + cfg.cooldownSec * 1000UL;
  stopSound();
  logEvent(EV_REPEL_END, 0, -1, msg);
}

// Chỉ xua đuổi khi CẢ HAI xác nhận: cảm biến PIR thấy chuyển động VÀ AI YOLO thấy sóc trong ảnh camera.
// Không chụp được ảnh hoặc không hỏi được AI (mất Internet) thì chỉ ghi nhật ký, không xua.
void processMotion() {
  todayMotion++;
  camera_fb_t *fb = nullptr;
  if (okCam) {   // bỏ khung cũ nằm sẵn trong bộ đệm, lấy ảnh chụp ngay lúc PIR báo
    fb = esp_camera_fb_get();
    if (fb) { esp_camera_fb_return(fb); fb = esp_camera_fb_get(); }
  }
  int photo = -1;
  if (fb && okSd && cfg.savePhoto) photo = savePhoto(fb);
  // Ảnh camera -> AI YOLO trên Vercel (~2 giây). Không có thẻ nhớ thì nhờ máy chủ giữ ảnh luôn.
  int aiPhoto = -1;
  int ai = fb ? cloudAi(fb, photo < 0, aiPhoto) : -1;
  if (fb) esp_camera_fb_return(fb);
  if (photo < 0) photo = aiPhoto;

  if (ai < 0) {
    lastScore = 0;
    logEvent(EV_MOTION, 0, photo, !fb ? "Chuyển động - camera không chụp được, không xua"
                                      : "Chuyển động - chưa hỏi được AI (mất Internet), không xua");
    return;
  }
  lastScore = ai;
  if (ai >= cfg.threshold) {
    if (leadSound >= 0) soundResult(false);   // sóc quay lại trong 10 phút: tiếng lượt trước chưa đủ sợ
    todaySquirrel++;
    hourly[hourNow()]++;
    lastDetectTs = nowTs();
    logEvent(EV_SQUIRREL, ai, photo, "PIR + AI camera xác nhận có sóc");
    if (photo >= 0 && photo < 50000) cloudPhoto(photo);   // ảnh >= 50000 đã nằm trên máy chủ
    if (cfg.autoMode) startRepel("tự động");
  } else {
    logEvent(EV_MOTION, ai, photo, "Chuyển động - AI: không phải sóc");
  }
}

bool pirPrev = false;
void checkMotion() {
  bool pir = digitalRead(PIN_PIR);
  sn.pir = pir;
  // Cạnh lên, hoặc PIR giữ mức HIGH liên tục quá 5s (vật còn đó: chụp lại cho AI xem thêm lần nữa)
  bool trigger = pir && (!pirPrev || millis() - lastMotionMs > 5000);
  pirPrev = pir;
  if (!trigger) return;
  lastMotionMs = millis();
  if (!cfg.armed || state != ST_IDLE) return;
  if (cfg.nightOnly && !sn.night) return;
  processMotion();
}

void updateOutputs() {
  uint32_t now = millis();
  if (state == ST_REPEL && (int32_t)(now - repelEndMs) >= 0)
    stopRepel(repelRoundsLeft ? "Hết lượt xua, nghỉ rồi xua tiếp" : "Kết thúc xua đuổi");
  if (state == ST_COOLDOWN && (int32_t)(now - cooldownEndMs) >= 0) {
    if (repelRoundsLeft) nextRepelRound();   // còn lượt: xua tiếp
    else state = ST_IDLE;
  }
  if (mistManual && now - mistManualAt > MIST_MAX_ON_MS) mistManual = false;

  bool ledWant = ledManual, mistWant = mistManual;
  if (state == ST_REPEL) {
    if (cfg.useLed) ledWant = (now / 150) % 2 == 0;   // nháy chớp
    if (cfg.useMist) mistWant = true;
  }
  if (ledWant != ledOut) { ledOut = ledWant; digitalWrite(PIN_LED_DRV, ledOut); }
  if (mistWant != mistOut) { mistOut = mistWant; digitalWrite(PIN_MIST_DRV, mistOut); }
}

// ---------------------------------------------------------------------
//  Màn hình OLED 0,96" (SSD1306 128x64, I2C) - không có thì bỏ qua
//  Font mặc định không có dấu tiếng Việt nên chữ viết không dấu.
// ---------------------------------------------------------------------
void oledMessage(const char *l1, const char *l2) {
  if (!okOled) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 8);
  oled.print(l1);
  oled.setTextSize(1);
  oled.setCursor(0, 40);
  oled.print(l2);
  oled.display();
}

void drawOled() {
  if (!okOled) return;
  uint32_t now = millis();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setTextWrap(false);

  // Dòng trên: tên + giờ
  oled.setCursor(0, 0);
  oled.print("DUOI SOC");
  oled.setCursor(98, 0);
  if (clockValid()) {
    uint32_t t = nowTs() % 86400UL;
    oled.printf("%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60));
  } else oled.print("--:--");
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // Trạng thái (chữ to) + chi tiết
  const char *big;
  char line[24];
  if (!cfg.armed) {
    big = "DA TAT";
    snprintf(line, sizeof(line), "Bat canh gac tren web");
  } else if (state == ST_REPEL) {
    big = "XUA DUOI!";
    snprintf(line, sizeof(line), "Con %lds  Diem %u", max(0L, (long)(repelEndMs - now)) / 1000 + 1, lastScore);
  } else if (state == ST_COOLDOWN) {
    big = "DANG NGHI";
    snprintf(line, sizeof(line), "Con %lds", max(0L, (long)(cooldownEndMs - now)) / 1000 + 1);
  } else {
    big = "CANH GAC";
    snprintf(line, sizeof(line), "%s  Diem cuoi %u", sn.pir ? "PIR:CO" : "PIR:--", lastScore);
  }
  oled.setTextSize(2);
  oled.setCursor(0, 14);
  oled.print(big);
  oled.setTextSize(1);
  oled.setCursor(0, 33);
  oled.print(line);

  // Thống kê hôm nay: số lần thấy sóc / xua đuổi / chuyển động
  oled.setCursor(0, 44);
  oled.printf("Soc %u  Xua %u  CD %u", todaySquirrel, todayRepel, todayMotion);

  // Địa chỉ mở web
  oled.setCursor(0, 56);
  if (WiFi.status() == WL_CONNECTED) oled.print(WiFi.localIP());
  else oled.print(WiFi.softAPIP());
  if (mistOut) { oled.setCursor(116, 56); oled.print("PS"); }   // đang phun sương

  // Đang xua đuổi: nháy đảo màu cho dễ thấy từ xa
  oled.invertDisplay(state == ST_REPEL && (now / 500) % 2);
  oled.display();
}

// ---------------------------------------------------------------------
//  Web server & API
// ---------------------------------------------------------------------
String jf(float v, int dec) { return isnan(v) ? String("null") : String(v, dec); }

String jstr(const char *s) {
  String o = "\"";
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') o += '\\';
    o += *s;
  }
  return o + "\"";
}

void sendJson(const String &s) { server.send(200, "application/json; charset=utf-8", s); }

String settingsJson() {
  String s;
  s.reserve(220);
  s += "{\"threshold\":" + String(cfg.threshold);
  s += ",\"repelSec\":" + String(cfg.repelSec);
  s += ",\"cooldownSec\":" + String(cfg.cooldownSec);
  s += ",\"repelRounds\":" + String(cfg.repelRounds);
  s += ",\"useSound\":" + String(cfg.useSound);
  s += ",\"useLed\":" + String(cfg.useLed);
  s += ",\"useMist\":" + String(cfg.useMist);
  s += ",\"nightOnly\":" + String(cfg.nightOnly);
  s += ",\"savePhoto\":" + String(cfg.savePhoto);
  s += ",\"volume\":" + String(cfg.volume) + "}";
  return s;
}

void handleRoot() {
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8", (const char *)INDEX_HTML_GZ, sizeof(INDEX_HTML_GZ));
}

String statusJson() {
  uint32_t now = millis();
  const char *st = state == ST_REPEL ? "repel" : state == ST_COOLDOWN ? "cooldown" : "idle";
  float repelLeft = state == ST_REPEL ? max(0L, (long)(repelEndMs - now)) / 1000.0 : 0;
  float coolLeft = state == ST_COOLDOWN ? max(0L, (long)(cooldownEndMs - now)) / 1000.0 : 0;

  String s;
  s.reserve(1200);
  s += "{\"ts\":" + String(nowTs());
  s += ",\"rtcOk\":" + String(okRtc && clockValid());
  s += ",\"uptime\":" + String(now / 1000);
  s += ",\"armed\":" + String(cfg.armed);
  s += ",\"auto\":" + String(cfg.autoMode);
  s += ",\"state\":\"" + String(st) + "\"";
  s += ",\"repelLeft\":" + String(repelLeft, 1);
  s += ",\"repelTotal\":" + String(cfg.repelSec);
  s += ",\"cooldownLeft\":" + String(coolLeft, 1);
  s += ",\"pir\":" + String(sn.pir);
  s += ",\"lux\":" + jf(sn.lux, 1);
  s += ",\"night\":" + String(sn.night ? "true" : "false");
  s += ",\"tempC\":" + jf(sn.tempC, 2);
  s += ",\"batV\":" + (isnan(sn.batV) ? String(0) : String(sn.batV, 2));
  s += ",\"batmA\":" + jf(sn.batmA, 0);
  s += ",\"batPct\":" + String(sn.batPct);
  s += ",\"led\":" + String(ledOut || (state == ST_REPEL && cfg.useLed));
  s += ",\"mist\":" + String(mistOut);
  s += ",\"sound\":" + String(soundOn);
  s += ",\"volume\":" + String(cfg.volume);
  s += ",\"tracks\":" + String(sndTracks > 0 ? sndTracks : BUILTIN_COUNT);
  if (sndTracks < 1) {   // tiếng có sẵn: tên + số lượt xua mở đầu bằng tiếng đó / số lần sóc bỏ chạy
    s += ",\"builtin\":1,\"sndNames\":[";
    for (int i = 0; i < BUILTIN_COUNT; i++) s += String(i ? ",\"" : "\"") + BUILTIN_SOUNDS[i].name + "\"";
    s += "],\"sndStat\":[";
    for (int i = 0; i < BUILTIN_COUNT; i++) s += String(i ? ",[" : "[") + sndTries[i] + "," + sndFled[i] + "]";
    s += "]";
  }
  s += ",\"today\":{\"motion\":" + String(todayMotion) + ",\"squirrel\":" + String(todaySquirrel) + ",\"repel\":" + String(todayRepel) + "}";
  s += ",\"hourly\":[";
  for (int i = 0; i < 24; i++) { if (i) s += ','; s += hourly[i]; }
  s += "],\"lastScore\":" + String(lastScore);
  s += ",\"lastEventId\":" + String(lastEventId);
  s += ",\"lastDetectTs\":" + String(lastDetectTs);
  s += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  s += ",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\"";
  s += ",\"apIp\":\"" + WiFi.softAPIP().toString() + "\"";
  s += ",\"apSsid\":" + jstr(AP_SSID);
  s += ",\"mods\":{\"rtc\":" + String(okRtc) + ",\"lux\":" + String(okLux) + ",\"ina\":" + String(okIna) +
       ",\"df\":" + String(okSound) + ",\"cam\":" + String(okCam) + ",\"sd\":" + String(okSd) + "}";
  char ce[12];
  snprintf(ce, sizeof(ce), "0x%x", camErr);
  s += ",\"camErr\":\"" + String(ce) + "\"";
  s += ",\"net\":" + netJson();
  s += ",\"cfg\":" + settingsJson() + "}";
  return s;
}

void handleStatus() { sendJson(statusJson()); }

String eventsJson() {
  String s;
  s.reserve(evCount * 110 + 4);
  s += '[';
  for (int i = 0; i < evCount; i++) {
    const Event &e = events[(evHead - 1 - i + MAX_EVENTS) % MAX_EVENTS];
    if (i) s += ',';
    s += "{\"id\":" + String(e.id) + ",\"ts\":" + String(e.ts) + ",\"t\":\"" + EV_NAMES[e.type] +
         "\",\"score\":" + String(e.score) + ",\"photo\":" + String(e.photo) + ",\"msg\":" + jstr(e.msg) + "}";
  }
  s += ']';
  return s;
}

void handleEvents() { sendJson(eventsJson()); }

void handleSettings() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("threshold"))   cfg.threshold   = constrain(server.arg("threshold").toInt(), 0, 100);
    if (server.hasArg("repelSec"))    cfg.repelSec    = constrain(server.arg("repelSec").toInt(), 1, 300);
    if (server.hasArg("cooldownSec")) cfg.cooldownSec = constrain(server.arg("cooldownSec").toInt(), 0, 3600);
    if (server.hasArg("repelRounds")) cfg.repelRounds = constrain(server.arg("repelRounds").toInt(), 1, 5);
    if (server.hasArg("volume"))      cfg.volume      = constrain(server.arg("volume").toInt(), 0, 30);
    if (server.hasArg("useSound"))    cfg.useSound    = server.arg("useSound").toInt();
    if (server.hasArg("useLed"))      cfg.useLed      = server.arg("useLed").toInt();
    if (server.hasArg("useMist"))     cfg.useMist     = server.arg("useMist").toInt();
    if (server.hasArg("nightOnly"))   cfg.nightOnly   = server.arg("nightOnly").toInt();
    if (server.hasArg("savePhoto"))   cfg.savePhoto   = server.arg("savePhoto").toInt();
    saveSettings();
    logEvent(EV_MANUAL, 0, -1, "Cập nhật cài đặt");
  }
  sendJson(settingsJson());
}

bool runCommand(const String &c, int v) {
  char m[64];

  if (c == "arm") {
    cfg.armed = v; saveSettings();
    if (!v) { repelRoundsLeft = 0; stopRepel("Dừng do tắt canh gác"); }
    logEvent(EV_MANUAL, 0, -1, v ? "Bật canh gác" : "Tắt canh gác");
  } else if (c == "auto") {
    cfg.autoMode = v; saveSettings();
    logEvent(EV_MANUAL, 0, -1, v ? "Chuyển sang tự động" : "Chuyển sang thủ công");
  } else if (c == "repel") {
    startRepel("thủ công");
  } else if (c == "led") {
    ledManual = v;
    logEvent(EV_MANUAL, 0, -1, v ? "Bật đèn LED" : "Tắt đèn LED");
  } else if (c == "mist") {
    mistManual = v; mistManualAt = millis();
    logEvent(EV_MANUAL, 0, -1, v ? "Bật phun mùi" : "Tắt phun mùi");
  } else if (c == "sound") {
    int t = constrain(v, 1, sndTracks < 1 ? BUILTIN_COUNT : max(1, sndTracks));
    if (sndTracks < 1 ? playBuiltin(t - 1) : playTrack(t)) {
      if (sndTracks < 1) snprintf(m, sizeof(m), "Phát tiếng %s", BUILTIN_SOUNDS[t - 1].name);
      else snprintf(m, sizeof(m), "Phát âm thanh %d.wav", t);
      logEvent(EV_MANUAL, 0, -1, m);
    }
  } else if (c == "stopsound") {
    stopSound();
  } else if (c == "volume") {
    cfg.volume = constrain(v, 0, 30); saveSettings();
  } else if (c == "photo") {
    camera_fb_t *fb = okCam ? esp_camera_fb_get() : nullptr;
    int photo = -1;
    if (fb) { if (okSd) photo = savePhoto(fb); esp_camera_fb_return(fb); }
    logEvent(EV_MANUAL, 0, photo, "Chụp ảnh thủ công");
    cloudPhoto(photo);
  } else if (c == "stop") {
    repelRoundsLeft = 0;
    stopRepel("Dừng xua đuổi (thủ công)");
    ledManual = mistManual = false;
    stopSound();
    logEvent(EV_MANUAL, 0, -1, "Dừng tất cả");
  } else {
    return false;
  }
  return true;
}

void handleControl() {
  if (runCommand(server.arg("cmd"), server.arg("v").toInt())) sendJson("{\"ok\":true}");
  else server.send(400, "application/json", "{\"ok\":false,\"err\":\"lenh khong hop le\"}");
}

void handleTime() {
  uint32_t ts = strtoul(server.arg("ts").c_str(), nullptr, 10);
  if (ts < 1600000000UL) { server.send(400, "application/json", "{\"ok\":false}"); return; }
  if (okRtc) rtc.adjust(DateTime(ts));
  clockBaseTs = ts;
  clockBaseMs = millis();
  checkDayRollover();
  logEvent(EV_SYSTEM, 0, -1, "Đồng bộ giờ từ trình duyệt");
  sendJson("{\"ok\":true}");
}

void handleCapture() {
  camera_fb_t *fb = okCam ? esp_camera_fb_get() : nullptr;
  if (!fb) { server.send(503, "text/plain", "Camera loi"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// Ảnh giao diện (cây sầu riêng, sóc...) nhúng sẵn trong firmware, phục vụ tại /img/<tên file>
bool serveAsset(const String &uri) {
  for (size_t i = 0; i < WEB_ASSET_COUNT; i++) {
    if (uri == WEB_ASSETS[i].path) {
      server.sendHeader("Cache-Control", "max-age=604800");
      server.send_P(200, WEB_ASSETS[i].mime, (const char *)WEB_ASSETS[i].data, WEB_ASSETS[i].len);
      return true;
    }
  }
  return false;
}

void handlePhoto() {
  char path[32];
  snprintf(path, sizeof(path), "/events/%05d.jpg", (int)server.arg("id").toInt());
  File f = okSd ? SD_MMC.open(path) : File();
  if (!f) { server.send(404, "text/plain", "Khong co anh"); return; }
  server.sendHeader("Cache-Control", "max-age=86400");
  server.streamFile(f, "image/jpeg");
  f.close();
}

// ---------------------------------------------------------------------
void setup() {
  pinMode(PIN_LED_DRV, OUTPUT);  digitalWrite(PIN_LED_DRV, LOW);
  pinMode(PIN_MIST_DRV, OUTPUT); digitalWrite(PIN_MIST_DRV, LOW);
  pinMode(PIN_PIR, INPUT);

  Serial.begin(115200);
  delay(800);
  Serial.println("\n*** HE THONG DUOI SOC THONG MINH ***");
  loadSettings();
  loadSoundStats();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  Serial.print("I2C (GPIO21/47):");   // liệt kê module I2C tìm thấy để dễ kiểm tra dây
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
  }
  Serial.println();
  okRtc = rtc.begin(&Wire);
  okLux = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, ADDR_BH1750, &Wire);
  okIna = ina219.begin(&Wire);
  okOled = oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
  oledMessage("DUOI SOC", "Dang khoi dong...");

  okCam = initCamera();
  okSd = initSd();
  okSound = initSound();
  sndTracks = okSd ? countTracks() : 0;
  syncClockFromRtc();
  readSensors();

  Serial.printf("RTC:%d  BH1750:%d  INA219:%d  OLED:%d  Loa I2S:%d (%d file .wav)  Camera:%d  SD:%d\n",
                okRtc, okLux, okIna, okOled, okSound, sndTracks, okCam, okSd);

  // WiFi: luôn phát WiFi riêng + kết nối WiFi nhà vườn nếu có cấu hình
  wifiEvents();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);
  String ssid = prefs.getString("wssid", WIFI_SSID), pass = prefs.getString("wpass", WIFI_PASS);
  if (ssid.length()) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("Dang ket noi WiFi '%s'", ssid.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) { delay(300); Serial.print('.'); }
    Serial.println();
  }
  cloudBegin();
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);

  server.enableCORS(true);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/events", HTTP_GET, handleEvents);
  server.on("/api/settings", handleSettings);
  server.on("/api/control", HTTP_POST, handleControl);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/photo", HTTP_GET, handlePhoto);
  server.on("/api/net", handleNet);
  server.onNotFound([]() {
    if (serveAsset(server.uri())) return;
    server.send(404, "text/plain", "Khong tim thay");
  });
  server.begin();
  if (okCam) startStream();   // video MJPEG cổng 81 (stream.ino)

  Serial.println("==============================================");
  Serial.printf(" WiFi rieng: %s / %s  ->  http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf(" WiFi vuon : http://%s  hoac  http://%s.local\n", WiFi.localIP().toString().c_str(), MDNS_NAME);
  Serial.println("==============================================");

  logEvent(EV_SYSTEM, 0, -1, "Khởi động hệ thống");
}

void loop() {
  static uint32_t tSensor = 0, tClock = 0, tOled = 0;
  static State oledState = ST_IDLE;
  uint32_t now = millis();

  server.handleClient();
  checkMotion();
  updateOutputs();
  soundLoop();
  cloudLoop();
  if (leadSound >= 0 && millis() - leadAt >= SND_RETURN_MS) soundResult(true);   // 10 phút sóc không quay lại

  if (now - tSensor >= 1000) { tSensor = now; readSensors(); checkDayRollover(); }
  if (now - tClock >= 60000) { tClock = now; syncClockFromRtc(); }
  // OLED: vẽ lại mỗi 0,5s, hoặc ngay khi đổi trạng thái
  if (now - tOled >= 500 || state != oledState) { tOled = now; oledState = state; drawOled(); }

  delay(2);
}
