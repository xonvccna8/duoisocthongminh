// =====================================================================
//  Video MJPEG trong mạng nhà: http://<ip thiết bị>:81/stream  (~15 ảnh/giây)
//  Giống firmware trạm 1 của dự án Huỳnh Thái Nguyên: chạy trên esp_http_server riêng (cổng 81),
//  mỗi người xem một task riêng nên xem video không làm chậm loop() (PIR, AI, loa, web cổng 80).
//  Web Vercel (https) không mở được luồng này (trình duyệt chặn http trong trang https): ở xa vẫn
//  xem ảnh gửi qua Internet ~2 ảnh/giây, ở nhà mở http://<ip thiết bị> để xem mượt.
// =====================================================================
// esp_http_server.h đã include trong duoi_soc_web.ino (prototype Arduino tự sinh cần thấy httpd_req_t)

const uint8_t STREAM_FPS = 15;              // trần tốc độ: chừa sóng WiFi và CPU cho AI, gửi lên Internet
const uint8_t STREAM_MAX_CLIENTS = 2;
const size_t STREAM_BUF_BYTES = 96 * 1024;  // khung VGA JPEG ~15-40 KB
httpd_handle_t streamHttpd = nullptr;
volatile uint8_t streamClients = 0;

void streamTask(void *arg) {
  httpd_req_t *req = (httpd_req_t *)arg;
  // Chép khung ra bộ đệm riêng (PSRAM) rồi trả khung ngay: người xem mạng chậm không giữ camera
  uint8_t *buf = (uint8_t *)heap_caps_malloc(STREAM_BUF_BYTES, MALLOC_CAP_SPIRAM);
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=duoisocframe");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");   // để AI trên trang web đọc được khung hình
  char head[96];
  esp_err_t r = buf ? ESP_OK : ESP_FAIL;
  uint32_t lastMs = 0;
  while (r == ESP_OK && okCam) {
    uint32_t since = millis() - lastMs;
    if (lastMs && since < 1000 / STREAM_FPS) vTaskDelay(pdMS_TO_TICKS(1000 / STREAM_FPS - since));
    lastMs = millis();
    size_t len = 0;
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb && fb->len <= STREAM_BUF_BYTES) { memcpy(buf, fb->buf, fb->len); len = fb->len; }
    if (fb) esp_camera_fb_return(fb);
    if (!len) continue;
    int n = snprintf(head, sizeof(head), "\r\n--duoisocframe\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                     (unsigned)len);
    r = httpd_resp_send_chunk(req, head, n);
    if (r == ESP_OK) r = httpd_resp_send_chunk(req, (const char *)buf, len);
  }
  if (buf) heap_caps_free(buf);
  httpd_req_async_handler_complete(req);
  streamClients--;
  vTaskDelete(nullptr);
}

esp_err_t streamHandler(httpd_req_t *req) {
  if (!okCam) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "camera loi", HTTPD_RESP_USE_STRLEN);
  }
  if (streamClients >= STREAM_MAX_CLIENTS) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "dang co 2 nguoi xem video", HTTPD_RESP_USE_STRLEN);
  }
  httpd_req_t *copy = nullptr;
  if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) return ESP_FAIL;
  streamClients++;
  if (xTaskCreatePinnedToCore(streamTask, "mjpeg", 6144, copy, 2, nullptr, 0) != pdPASS) {
    streamClients--;
    httpd_req_async_handler_complete(copy);
    return ESP_FAIL;
  }
  return ESP_OK;
}

void startStream() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 81;
  cfg.ctrl_port = 32769;
  cfg.max_open_sockets = 4;
  cfg.lru_purge_enable = true;   // trình duyệt mở lại thì đóng kết nối video cũ
  httpd_uri_t uri = {"/stream", HTTP_GET, streamHandler, nullptr};
  if (httpd_start(&streamHttpd, &cfg) == ESP_OK && httpd_register_uri_handler(streamHttpd, &uri) == ESP_OK)
    Serial.printf("Video: http://<ip>:81/stream (toi da %u anh/giay)\n", STREAM_FPS);
  else
    Serial.println("[VIDEO] Khong mo duoc cong 81");
}
