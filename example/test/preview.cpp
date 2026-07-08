#include <Arduino.h>
#include "esp_camera.h"
#include "camera_drv.h"
#include "display_drv.h"
#include "pin_config.h"
#include "preview.h"

namespace
{
    constexpr uint16_t kPreviewWidth = LCD_WIDTH;
    constexpr uint16_t kPreviewHeight = LCD_HEIGHT;
    constexpr uint16_t kPreviewOffsetX = 0;
    constexpr uint16_t kPreviewOffsetY = 0;

    bool previewReady = false;
    uint16_t *previewBuffer = nullptr;
    uint32_t previewFrameCount = 0;
    uint32_t previewFailCount = 0;
    uint32_t lastPreviewReportMs = 0;

    uint16_t swapRgb565Bytes(uint16_t value)
    {
        return static_cast<uint16_t>((value >> 8) | (value << 8));
    }

    bool transformFrameToPreview(const camera_fb_t *frame)
    {
        if (frame == nullptr || frame->buf == nullptr)
        {
            return false;
        }

        if (frame->format != PIXFORMAT_RGB565)
        {
            Serial.printf("[PREVIEW] Unsupported frame format=%d\n", static_cast<int>(frame->format));
            return false;
        }

        const uint16_t *src = reinterpret_cast<const uint16_t *>(frame->buf);
        const int srcWidth = frame->width;
        const int srcHeight = frame->height;

        int visibleSrcWidth = srcWidth;
        int visibleSrcHeight = srcHeight;
        int cropSrcX = 0;
        int cropSrcY = 0;

        if (kPreviewWidth * srcHeight >= kPreviewHeight * srcWidth)
        {
            visibleSrcHeight = (srcWidth * kPreviewHeight) / kPreviewWidth;
            cropSrcY = (srcHeight - visibleSrcHeight) / 2;
        }
        else
        {
            visibleSrcWidth = (srcHeight * kPreviewWidth) / kPreviewHeight;
            cropSrcX = (srcWidth - visibleSrcWidth) / 2;
        }

        for (int destY = 0; destY < kPreviewHeight; ++destY)
        {
            int srcY = cropSrcY + (destY * visibleSrcHeight) / kPreviewHeight;
            if (srcY >= srcHeight)
            {
                srcY = srcHeight - 1;
            }

            for (int destX = 0; destX < kPreviewWidth; ++destX)
            {
                int srcX = cropSrcX + (destX * visibleSrcWidth) / kPreviewWidth;
                if (srcX >= srcWidth)
                {
                    srcX = srcWidth - 1;
                }

                const uint16_t pixel = src[srcY * srcWidth + srcX];
                previewBuffer[destY * kPreviewWidth + destX] = swapRgb565Bytes(pixel);
            }
        }

        return true;
    }
}

bool previewInit()
{
    if (previewReady)
    {
        return true;
    }

    previewBuffer = static_cast<uint16_t *>(ps_malloc(kPreviewWidth * kPreviewHeight * sizeof(uint16_t)));
    if (previewBuffer == nullptr)
    {
        Serial.println("[PREVIEW] Preview buffer alloc failed");
        return false;
    }

    previewFrameCount = 0;
    previewFailCount = 0;
    lastPreviewReportMs = millis();
    previewReady = true;

    Serial.printf(
        "[PREVIEW] Preview init passed: %ux%u at (%u,%u)\n",
        static_cast<unsigned>(kPreviewWidth),
        static_cast<unsigned>(kPreviewHeight),
        static_cast<unsigned>(kPreviewOffsetX),
        static_cast<unsigned>(kPreviewOffsetY));
    return true;
}

void previewLoop()
{
    if (!previewReady)
    {
        delay(1000);
        return;
    }

    camera_fb_t *frame = cameraCaptureFrame();
    if (frame == nullptr)
    {
        ++previewFailCount;
        uint32_t now = millis();
        if (now - lastPreviewReportMs >= 1000)
        {
            Serial.printf(
                "[PREVIEW] Preview retrying frames=%u fails=%u\n",
                static_cast<unsigned>(previewFrameCount),
                static_cast<unsigned>(previewFailCount));
            lastPreviewReportMs = now;
        }
        delay(30);
        return;
    }

    uint16_t srcWidth = frame->width;
    uint16_t srcHeight = frame->height;
    bool transformOk = transformFrameToPreview(frame);
    cameraReleaseFrame(frame);
    if (!transformOk)
    {
        ++previewFailCount;
        delay(30);
        return;
    }

    displayDrawRgb565Bitmap(
        kPreviewOffsetX,
        kPreviewOffsetY,
        previewBuffer,
        kPreviewWidth,
        kPreviewHeight);

    ++previewFrameCount;

    uint32_t now = millis();
    if (now - lastPreviewReportMs >= 1000)
    {
        float fps = 0.0f;
        uint32_t elapsed = now - lastPreviewReportMs;
        if (elapsed > 0)
        {
            fps = (previewFrameCount * 1000.0f) / elapsed;
        }

        Serial.printf(
            "[PREVIEW] Preview frames=%u fails=%u src=%ux%u dst=%ux%u\n",
            static_cast<unsigned>(previewFrameCount),
            static_cast<unsigned>(previewFailCount),
            static_cast<unsigned>(srcWidth),
            static_cast<unsigned>(srcHeight),
            static_cast<unsigned>(kPreviewWidth),
            static_cast<unsigned>(kPreviewHeight));
        Serial.printf("[PREVIEW] Approx FPS=%.2f\n", fps);

        previewFrameCount = 0;
        previewFailCount = 0;
        lastPreviewReportMs = now;
    }
}
