#include <Arduino.h>
#include "esp_camera.h"
#include "pin_config.h"
#include "camera_drv.h"

namespace
{
    bool cameraReady = false;
    uint32_t frameCount = 0;
    uint32_t frameFailCount = 0;
    uint32_t lastReportMs = 0;
    size_t lastFrameLength = 0;
    uint16_t lastFrameWidth = 0;
    uint16_t lastFrameHeight = 0;
    int lastPixelFormat = -1;

    void printFrameInfo(const camera_fb_t *frame, const char *tag)
    {
        Serial.printf(
            "%s width=%u height=%u len=%u format=%d\n",
            tag,
            static_cast<unsigned>(frame->width),
            static_cast<unsigned>(frame->height),
            static_cast<unsigned>(frame->len),
            static_cast<int>(frame->format));
    }

    void powerOnCamera()
    {
        pinMode(CAM_POWER_EN, OUTPUT);
        digitalWrite(CAM_POWER_EN, LOW);
        delay(20);
    }
}

bool cameraInit()
{
    powerOnCamera();

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAM_Y2;
    config.pin_d1 = CAM_Y3;
    config.pin_d2 = CAM_Y4;
    config.pin_d3 = CAM_Y5;
    config.pin_d4 = CAM_Y6;
    config.pin_d5 = CAM_Y7;
    config.pin_d6 = CAM_Y8;
    config.pin_d7 = CAM_Y9;
    config.pin_xclk = CAM_XCLK;
    config.pin_pclk = CAM_PCLK;
    config.pin_vsync = CAM_VSYNC;
    config.pin_href = CAM_HREF;
    config.pin_sccb_sda = CAM_SIOD;
    config.pin_sccb_scl = CAM_SIOC;
    config.pin_pwdn = CAM_PWDN;
    config.pin_reset = CAM_RESET;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("[STAGE4] Camera init failed: 0x%x (power_en=%d)\n", static_cast<unsigned>(err), CAM_POWER_EN);
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr)
    {
        Serial.println("[STAGE4] Sensor handle is null");
        return false;
    }

    if (sensor->id.PID == OV3660_PID)
    {
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, 0);
        sensor->set_brightness(sensor, 1);
        sensor->set_saturation(sensor, 0);
    }

    frameCount = 0;
    frameFailCount = 0;
    lastReportMs = millis();
    lastFrameLength = 0;
    lastFrameWidth = 0;
    lastFrameHeight = 0;
    lastPixelFormat = -1;
    cameraReady = true;

    Serial.println("[STAGE4] Camera init passed");
    return true;
}

bool cameraPrintSensorInfo()
{
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr)
    {
        Serial.println("[STAGE4] Sensor info unavailable");
        return false;
    }

    Serial.printf(
        "[STAGE4] Sensor PID=0x%02x VER=0x%02x MIDH=0x%02x MIDL=0x%02x\n",
        static_cast<unsigned>(sensor->id.PID),
        static_cast<unsigned>(sensor->id.VER),
        static_cast<unsigned>(sensor->id.MIDH),
        static_cast<unsigned>(sensor->id.MIDL));
    return true;
}

bool cameraCaptureOnce()
{
    if (!cameraReady)
    {
        Serial.println("[STAGE4] Camera not ready");
        return false;
    }

    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr)
    {
        Serial.println("[STAGE4] Single capture failed");
        return false;
    }

    lastFrameWidth = frame->width;
    lastFrameHeight = frame->height;
    lastFrameLength = frame->len;
    lastPixelFormat = frame->format;

    printFrameInfo(frame, "[STAGE4] Single frame");
    esp_camera_fb_return(frame);
    return true;
}

camera_fb_t *cameraCaptureFrame()
{
    if (!cameraReady)
    {
        return nullptr;
    }

    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr)
    {
        ++frameFailCount;
        return nullptr;
    }

    lastFrameWidth = frame->width;
    lastFrameHeight = frame->height;
    lastFrameLength = frame->len;
    lastPixelFormat = frame->format;
    ++frameCount;
    return frame;
}

void cameraReleaseFrame(camera_fb_t *frame)
{
    if (frame == nullptr)
    {
        return;
    }

    esp_camera_fb_return(frame);
}
