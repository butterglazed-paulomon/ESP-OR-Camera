#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include <ESP32-RTSPServer.h>

// ------------- WIFI CONFIG -------------
const char* WIFI_SSID     = "SASD-Office";
const char* WIFI_PASSWORD = "bzuryUW3";

// ------------- FREEnove ESP32-S3 CAMERA PINS -------------
#define CAM_PIN_PWDN    -1     // Power down pin not used
#define CAM_PIN_RESET   -1     // Reset pin not used

#define CAM_PIN_SIOD    4      // CAM_SIOD
#define CAM_PIN_SIOC    5      // CAM_SIOC
#define CAM_PIN_VSYNC   6      // CAM_VSYNC
#define CAM_PIN_HREF    7      // CAM_HREF
#define CAM_PIN_XCLK    15     // CAM_XCLK

#define CAM_PIN_Y9      16     // CAM_Y9
#define CAM_PIN_Y8      17     // CAM_Y8
#define CAM_PIN_Y7      18     // CAM_Y7
#define CAM_PIN_Y4      8      // CAM_Y4
#define CAM_PIN_Y3      9      // CAM_Y3
#define CAM_PIN_Y5      10     // CAM_Y5
#define CAM_PIN_Y2      11     // CAM_Y2
#define CAM_PIN_Y6      12     // CAM_Y6

#define CAM_PIN_PCLK    13     // CAM_PCLK

// ------------- RTSP SERVER OBJECTS -------------
RTSPServer rtspServer;
TaskHandle_t videoTaskHandle = nullptr;
int frameQuality = 15;

// ------------- WIFI HELPER -------------
void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP address: ");
  Serial.println(WiFi.localIP());
}

// ------------- CAMERA INIT -------------
bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0       = CAM_PIN_Y2;
  config.pin_d1       = CAM_PIN_Y3;
  config.pin_d2       = CAM_PIN_Y4;
  config.pin_d3       = CAM_PIN_Y5;
  config.pin_d4       = CAM_PIN_Y6;
  config.pin_d5       = CAM_PIN_Y7;
  config.pin_d6       = CAM_PIN_Y8;
  config.pin_d7       = CAM_PIN_Y9;

  config.pin_xclk     = CAM_PIN_XCLK;
  config.pin_pclk     = CAM_PIN_PCLK;
  config.pin_vsync    = CAM_PIN_VSYNC;
  config.pin_href     = CAM_PIN_HREF;

  // NOTE: use *sccb* (new name), not *sscb*
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;

  config.pin_pwdn     = CAM_PIN_PWDN;
  config.pin_reset    = CAM_PIN_RESET;

  config.xclk_freq_hz = 20000000;      // 20 MHz for OV2640
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size   = FRAMESIZE_VGA; // 640x480
  config.jpeg_quality = 15;            // lower = better quality
  config.fb_count     = 2;
  config.fb_location  = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  frameQuality = s->status.quality;
  Serial.printf("Camera initial quality: %d\n", frameQuality);

  return true;
}

// ------------- VIDEO TASK -------------
void sendVideoTask(void* pvParameters) {
  for (;;) {
    if (rtspServer.readyToSendFrame()) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      rtspServer.sendRTSPFrame(
        fb->buf,
        fb->len,
        frameQuality,
        fb->width,
        fb->height
      );

      esp_camera_fb_return(fb);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ------------- SETUP -------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("===== ESP32-S3 RTSP Camera (Freenove board) =====");

  connectWifi();

  if (!initCamera()) {
    Serial.println("Camera init failed, halting.");
    while (true) {
      delay(1000);
    }
  }

  // Configure RTSP: VIDEO ONLY, port 8554, video RTP port 5004
  rtspServer.transport      = RTSPServer::VIDEO_ONLY;
  rtspServer.maxRTSPClients = 3;
  rtspServer.setCredentials("", "");   // no auth

  bool ok = rtspServer.init(
      RTSPServer::VIDEO_ONLY,  // transport type
      8554,                    // RTSP control port
      0,                       // sampleRate (audio) not used
      5004                     // video RTP port
  );

  if (ok) {
    Serial.println("RTSP server started successfully.");
    Serial.print("RTSP URL: rtsp://");
    Serial.print(WiFi.localIP());
    Serial.println(":8554/");
  } else {
    Serial.println("Failed to start RTSP server.");
  }

  // Start video streaming task on core 1
  xTaskCreatePinnedToCore(
    sendVideoTask,
    "VideoTask",
    5 * 1024,
    nullptr,
    1,
    &videoTaskHandle,
    1
  );
}

// ------------- LOOP -------------
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
