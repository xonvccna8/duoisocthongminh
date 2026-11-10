// =====================================================================
//  XEM CAMERA QUA CÁP USB - để thử camera trên máy tính khi chưa có WiFi
//  Board ESP32-S3 CAM gửi ảnh JPEG liên tục qua cổng USB-UART (CH343) tốc độ 2 Mbit/s.
//  Trên máy tính chạy tools/xem_camera_usb.py rồi mở http://localhost:8090
//  Gói ảnh: 0xFF 0xAA 'J' 'P' + độ dài 4 byte (little-endian) + dữ liệu JPEG
//  Lệnh từ máy tính (1 byte): 'v' lật dọc, 'h' lật ngang, '+' / '-' độ phân giải
//  Nạp bằng FQBN mặc định (không CDCOnBoot) để Serial ra cổng UART.
// =====================================================================
#include "esp_camera.h"
#include "../duoi_soc_web/config.h"

const framesize_t SIZES[] = {FRAMESIZE_QVGA, FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_XGA};
int sizeIdx = 1;   // VGA 640x480 giống firmware chính
bool okCam = false;

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0;  c.pin_d1 = CAM_PIN_D1;  c.pin_d2 = CAM_PIN_D2;  c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4;  c.pin_d5 = CAM_PIN_D5;  c.pin_d6 = CAM_PIN_D6;  c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK;  c.pin_pclk = CAM_PIN_PCLK;  c.pin_vsync = CAM_PIN_VSYNC;  c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn = CAM_PIN_PWDN;  c.pin_reset = CAM_PIN_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;  c.frame_size = FRAMESIZE_XGA;  // cấp bộ đệm đủ cho cỡ lớn nhất
  c.jpeg_quality = 12;  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;  c.grab_mode = CAMERA_GRAB_LATEST;
  if (esp_camera_init(&c) != ESP_OK) return false;
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, SIZES[sizeIdx]);
  if (s->id.PID == OV3660_PID) {   // giống ví dụ CameraWebServer của Espressif
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  return true;
}

void setup() {
  Serial.setTxBufferSize(16384);
  Serial.begin(2000000);
  okCam = initCamera();
}

void loop() {
  sensor_t *s = okCam ? esp_camera_sensor_get() : nullptr;
  while (Serial.available()) {
    char c = Serial.read();
    if (!s) continue;
    if (c == 'v') s->set_vflip(s, !s->status.vflip);
    if (c == 'h') s->set_hmirror(s, !s->status.hmirror);
    if (c == '+' && sizeIdx < 3) s->set_framesize(s, SIZES[++sizeIdx]);
    if (c == '-' && sizeIdx > 0) s->set_framesize(s, SIZES[--sizeIdx]);
  }
  if (!okCam) {
    Serial.println("CAMERA_LOI");
    delay(1000);
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;
  uint8_t head[8] = {0xFF, 0xAA, 'J', 'P'};
  memcpy(head + 4, &fb->len, 4);
  Serial.write(head, 8);
  Serial.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}
