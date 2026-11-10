// =====================================================================
//  XEM TỪ XA QUA INTERNET
//  Khi board vào được WiFi nhà vườn, một tác vụ chạy nền (lõi 0) gửi lên web Vercel:
//    - trạng thái + nhật ký sự kiện: 60 giây/lần, 5 giây/lần khi có người đang xem web
//    - ảnh sóc vừa chụp (từ thẻ nhớ), và ảnh camera trực tiếp khi có người đang xem
//  và nhận lại lệnh điều khiển (xua đuổi, phun, ...) do người xem gửi (cần mã PIN).
//  Không có Internet thì mọi thứ trong vườn vẫn chạy bình thường như cũ.
// =====================================================================
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "mbedtls/sha256.h"

String devKey, pinHash;                     // khóa thiết bị (tự sinh, lưu trong board), mã băm PIN
volatile bool cloudFast = false, pushNow = false, ntpDone = false;
volatile uint32_t lastPushMs = 0;
char cloudState[48] = "chua co WiFi";

SemaphoreHandle_t cloudMx;
volatile bool wantPayload = false, payloadReady = false;
String payload;                             // loop() dựng, tác vụ nền gửi (bảo vệ bằng cloudMx)

struct CloudCmd { char c[12]; int v; };
CloudCmd cmdQ[8];
int32_t photoQ[8];
volatile uint8_t cmdHead = 0, cmdTail = 0, photoHead = 0, photoTail = 0;

String sha256Hex(const String &s) {
  uint8_t h[32];
  mbedtls_sha256((const uint8_t *)s.c_str(), s.length(), h, 0);
  char out[65];
  for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", h[i]);
  return String(out);
}

// Lý do lần cuối không vào được WiFi nhà (để web chỉ ra sai mật khẩu / không thấy WiFi)
volatile uint8_t staReason = 0;

void wifiEvents() {
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) staReason = info.wifi_sta_disconnected.reason;
    else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) staReason = 0;
  });
}

const char *staWhy() {
  switch (staReason) {
    case 0: return "";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
      return "khong-thay";   // sai tên, WiFi 5 GHz hoặc ở quá xa
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_EXPIRE:
      return "sai-mat-khau";
    default: return "khac";
  }
}

String netJson() {
  bool sta = WiFi.status() == WL_CONNECTED;
  String s = "{\"ssid\":" + jstr(prefs.getString("wssid", WIFI_SSID).c_str());
  s += ",\"sta\":" + String(sta ? "true" : "false");
  s += ",\"ip\":\"" + (sta ? WiFi.localIP().toString() : String("")) + "\"";
  s += ",\"cloud\":" + jstr(strlen(CLOUD_URL) ? cloudState : "tat");
  s += ",\"pushAgo\":" + String(lastPushMs ? (long)((millis() - lastPushMs) / 1000) : -1L);
  s += ",\"hasPass\":" + String(prefs.getString("wpass", WIFI_PASS).length() ? "true" : "false");
  s += ",\"why\":\"" + String(sta ? "" : staWhy()) + "\",\"reason\":" + String(staReason);
  s += ",\"pin\":" + String(pinHash.length() ? "true" : "false");
  s += ",\"url\":" + jstr(CLOUD_URL) + "}";
  return s;
}

// GET: tình trạng Internet. POST ssid/pass/pin: lưu WiFi nhà vườn và mã PIN điều khiển từ xa.
void handleNet() {
  if (server.method() == HTTP_POST) {
    String ssid = server.arg("ssid"), pass = server.arg("pass"), pin = server.arg("pin");
    bool changed = false;
    if (server.hasArg("ssid") && ssid != prefs.getString("wssid", "")) {
      prefs.putString("wssid", ssid); changed = true;
      if (!pass.length()) prefs.remove("wpass");
    }
    if (pass.length()) { prefs.putString("wpass", pass); changed = true; }
    if (pin.length() >= 4) { pinHash = sha256Hex(pin); prefs.putString("pinh", pinHash); pushNow = true; }
    if (changed) {
      WiFi.disconnect();
      String s = prefs.getString("wssid", ""), p = prefs.getString("wpass", "");
      if (s.length()) WiFi.begin(s.c_str(), p.c_str());
      logEvent(EV_SYSTEM, 0, -1, "Đổi WiFi nhà vườn");
    }
  }
  sendJson(netJson());
}

void cloudPhoto(int id) {
  if (id < 0 || !strlen(CLOUD_URL)) return;
  uint8_t n = (photoHead + 1) % 8;
  if (n != photoTail) { photoQ[photoHead] = id; photoHead = n; }
  pushNow = true;
}

// --- chạy trong tác vụ nền ---
NetworkClientSecure tls;
HTTPClient http;

int cloudPost(const String &path, const uint8_t *data, size_t len, const char *type, String *resp) {
  http.begin(tls, String(CLOUD_URL) + path);
  http.setTimeout(10000);
  http.addHeader("Content-Type", type);
  http.addHeader("X-Device-Key", devKey);
  int code = http.POST((uint8_t *)data, len);
  if (resp && code > 0) *resp = http.getString();
  http.end();
  return code;
}

// Phản hồi: {"ok":true,"fast":true,"cmds":[{"c":"repel","v":0,"at":...}]}
void parseReply(const String &r) {
  cloudFast = r.indexOf("\"fast\":true") >= 0;
  int i = 0;
  while ((i = r.indexOf("\"c\":\"", i)) >= 0) {
    i += 5;
    int e = r.indexOf('"', i);
    if (e < 0) break;
    int vp = r.indexOf("\"v\":", e);
    int nx = r.indexOf("\"c\":\"", e);
    int v = (vp >= 0 && (nx < 0 || vp < nx)) ? r.substring(vp + 4).toInt() : 0;
    uint8_t n = (cmdHead + 1) % 8;
    if (n != cmdTail) {
      strlcpy(cmdQ[cmdHead].c, r.substring(i, e).c_str(), sizeof(cmdQ[0].c));
      cmdQ[cmdHead].v = v;
      cmdHead = n;
    }
    i = e;
  }
}

bool pushStatus() {
  payloadReady = false;
  wantPayload = true;
  for (int i = 0; i < 300 && !payloadReady; i++) vTaskDelay(pdMS_TO_TICKS(10));
  wantPayload = false;
  if (!payloadReady) return false;
  xSemaphoreTake(cloudMx, portMAX_DELAY);
  String body = payload;
  payload = String();
  xSemaphoreGive(cloudMx);
  String resp;
  int code = cloudPost("/api/push", (const uint8_t *)body.c_str(), body.length(), "application/json", &resp);
  if (code == 200) {
    parseReply(resp);
    lastPushMs = millis();
    strlcpy(cloudState, "ok", sizeof(cloudState));
    return true;
  }
  snprintf(cloudState, sizeof(cloudState), code == 403 ? "sai khoa thiet bi (403)" : "loi gui %d", code);
  return false;
}

void uploadLive() {
  if (!okCam) return;
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;
  cloudPost("/api/push-photo?live=1", fb->buf, fb->len, "image/jpeg", nullptr);
  esp_camera_fb_return(fb);
}

void uploadEventPhoto(int id) {
  if (!okSd) return;
  char path[32];
  snprintf(path, sizeof(path), "/events/%05d.jpg", id);
  File f = SD_MMC.open(path);
  if (!f) return;
  size_t len = f.size();
  uint8_t *buf = (uint8_t *)(psramFound() ? ps_malloc(len) : malloc(len));
  if (buf && f.read(buf, len) == len) cloudPost("/api/push-photo?id=" + String(id), buf, len, "image/jpeg", nullptr);
  free(buf);
  f.close();
}

const uint32_t LIVE_GAP_MS = 250;   // có người xem: gửi ảnh camera liên tục (~1,5-2 ảnh/giây, mỗi lần gửi ~0,4 s)
// Máy tính ở nhà đang xem video cổng 81 (stream.ino) và phát lại ra Internet 12 ảnh/giây: gửi ảnh lên Vercel thưa
// thôi (2 giây/ảnh). Gửi HTTPS liên tục rất nặng với ESP32, làm cả video lẫn ảnh gửi lên đều tụt còn 1-5 ảnh/giây.
const uint32_t LIVE_GAP_RELAY_MS = 2000;
extern volatile uint8_t streamClients;

void cloudTask(void *) {
  tls.setInsecure();   // không kiểm tra chứng chỉ (đủ dùng cho mô hình; dữ liệu vẫn được mã hóa)
  http.setReuse(true);
  uint32_t lastLive = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      strlcpy(cloudState, prefs.isKey("wssid") || strlen(WIFI_SSID) ? "dang vao WiFi" : "chua co WiFi", sizeof(cloudState));
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (pushNow || !lastPushMs || millis() - lastPushMs >= (cloudFast ? 5000UL : 60000UL)) {
      pushNow = false;
      if (!pushStatus() && !lastPushMs) vTaskDelay(pdMS_TO_TICKS(5000));   // chưa gửi được lần nào: chờ rồi thử lại
    }
    uint32_t gap = streamClients ? LIVE_GAP_RELAY_MS : LIVE_GAP_MS;
    if (cloudFast && lastPushMs && millis() - lastLive >= gap) { lastLive = millis(); uploadLive(); }
    while (photoTail != photoHead) {
      uploadEventPhoto(photoQ[photoTail]);
      photoTail = (photoTail + 1) % 8;
    }
    vTaskDelay(pdMS_TO_TICKS(cloudFast ? 20 : 200));
  }
}

void cloudBegin() {
  devKey = prefs.getString("devkey", "");
  if (devKey.length() < 32) {   // lần đầu: tự sinh khóa thiết bị ngẫu nhiên, lưu mãi trong board
    char k[33];
    for (int i = 0; i < 16; i++) sprintf(k + i * 2, "%02x", (uint8_t)esp_random());
    devKey = k;
    prefs.putString("devkey", devKey);
  }
  pinHash = prefs.getString("pinh", "");
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // lấy giờ chuẩn khi có Internet
  if (!strlen(CLOUD_URL)) return;
  cloudMx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(cloudTask, "cloud", 12288, nullptr, 1, nullptr, 0);
}

// AI nhận diện sóc trên Vercel (gọi trong loop lúc PIR báo). Trả điểm 0-100, hoặc -1 khi không có Internet / lỗi
// (khi đó không xua đuổi tự động). keep = nhờ máy chủ lưu ảnh (board không có thẻ nhớ) -> photo = số ảnh trên máy chủ.
NetworkClientSecure aiTls;
HTTPClient aiHttp;

int cloudAi(camera_fb_t *fb, bool keep, int &photo) {
  photo = -1;
  if (!strlen(CLOUD_URL) || WiFi.status() != WL_CONNECTED || strcmp(cloudState, "ok")) return -1;
  static bool init = false;
  if (!init) { aiTls.setInsecure(); aiHttp.setReuse(true); init = true; }
  uint32_t t0 = millis();
  aiHttp.begin(aiTls, String(CLOUD_URL) + "/api/ai?keep=" + (keep ? "1" : "0"));
  aiHttp.setTimeout(9000);
  aiHttp.addHeader("Content-Type", "image/jpeg");
  aiHttp.addHeader("X-Device-Key", devKey);
  int code = aiHttp.POST(fb->buf, fb->len);
  String r = code == 200 ? aiHttp.getString() : String();
  aiHttp.end();
  int i = r.indexOf("\"score\":");
  if (code != 200 || i < 0) {
    Serial.printf("[AI] Loi %d, khong xua\n", code);
    return -1;
  }
  int score = constrain(r.substring(i + 8).toInt(), 0, 100);
  int j = r.indexOf("\"photo\":");
  if (j >= 0) photo = r.substring(j + 8).toInt();
  Serial.printf("[AI] Diem soc %d (%lu ms)\n", score, (unsigned long)(millis() - t0));
  return score;
}

// Gọi trong loop(): dựng dữ liệu cho tác vụ nền, chạy lệnh từ xa, đồng bộ giờ Internet
void cloudLoop() {
  static uint32_t lastEv = 0, tNtp = 0;
  if (time(nullptr) > 1700000000 && (!ntpDone || millis() - tNtp > 3600000UL)) {
    tNtp = millis();
    clockBaseTs = time(nullptr) + TZ_OFFSET_SEC;
    clockBaseMs = millis();
    if (okRtc) rtc.adjust(DateTime(clockBaseTs));
    if (!ntpDone) { ntpDone = true; checkDayRollover(); Serial.println("[GIO] Da lay gio qua Internet"); }
  }
  if (!strlen(CLOUD_URL)) return;
  if (lastEventId != lastEv) { lastEv = lastEventId; pushNow = true; }   // có sự kiện mới: gửi ngay
  if (wantPayload && !payloadReady) {
    String p = "{\"status\":" + statusJson() + ",\"events\":" + eventsJson() + ",\"pinHash\":\"" + pinHash + "\"}";
    xSemaphoreTake(cloudMx, portMAX_DELAY);
    payload = p;
    xSemaphoreGive(cloudMx);
    payloadReady = true;
  }
  while (cmdTail != cmdHead) {
    CloudCmd c = cmdQ[cmdTail];
    cmdTail = (cmdTail + 1) % 8;
    Serial.printf("[TU XA] Lenh %s %d\n", c.c, c.v);
    runCommand(String(c.c), c.v);
  }
}
