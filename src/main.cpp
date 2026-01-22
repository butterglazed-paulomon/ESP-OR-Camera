#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ==========================================
// 1. NETWORK CONFIG
// ==========================================
const char* SSID_NAME = "SASD-Office";
const char* SSID_PASS = "bzuryUW3";
const char* HOSTNAME  = "or-cam"; 

// ==========================================
// 2. PIN DEFINITIONS (Freenove S3)
// ==========================================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

httpd_handle_t stream_httpd = NULL;

// ==========================================
// 3. OPTIMIZED STREAM HANDLER
// ==========================================
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];
    static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
    static const char* _STREAM_BOUNDARY = "\r\n--frame\r\n";
    static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    // Loop for the video stream
    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        } else {
            // Fast path: Send header, buffer, boundary
            size_t hlen = snprintf(part_buf, 64, _STREAM_PART, fb->len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
            
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
            }
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, 12);
            }

            esp_camera_fb_return(fb);
            fb = NULL;
        }

        if (res != ESP_OK) break;
    }
    return res;
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    // Increase stack size for the server task to prevent crashes at high speed
    config.stack_size = 6144; 

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

    // ========================================================
    // CRITICAL DIAGNOSTIC
    // ========================================================
    // We expect ~8 MB (8388608 bytes). 
    // If you see "0" here, the camera will NEVER be fast.
    size_t psramSize = ESP.getPsramSize();
    Serial.printf("\n\n-----------------------------------\n");
    Serial.printf("PSRAM Check: %d bytes\n", psramSize);
    Serial.printf("-----------------------------------\n\n");

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    
    // 20MHz is standard for OV5640
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    // --- SPEED TUNING ---
    // VGA (640x480) is the only way to get consistent 30FPS on WiFi
    config.frame_size = FRAMESIZE_VGA;
    
    // Quality: 10 (High) to 63 (Low). 
    // 12 is a great balance.
    config.jpeg_quality = 12;

    if (psramSize > 0) {
        config.fb_count = 2; // Double buffering required for smooth video
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        Serial.println("!!! ERROR: PSRAM NOT WORKING. VIDEO WILL BE SLOW !!!");
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }

    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Camera Init Failed!");
        return;
    }

    WiFi.setSleep(false); // Max Performance
    WiFi.begin(SSID_NAME, SSID_PASS);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    if (MDNS.begin(HOSTNAME)) {
        Serial.println("MDNS started");
    }

    startCameraServer();
    
    Serial.printf("\nReady! Stream: http://%s/stream\n", WiFi.localIP().toString().c_str());
}

void loop() {
    delay(10000); 
}