/*
Library version:
Arduino IDE 2.3.6
esp32 V3.1.0
GFX Library for Arduino v1.5.6

Tools:
Flash size: 16MB(128Mb)
Partition Schrme: 16M Flash(3MB APP/9.9MB FATFS)
USB CDC On Boot  Enabled
PSRAM: OPI PSRAM
*/

#include <Arduino.h>
#include "camera_drv.h"
#include "display_drv.h"
#include "preview.h"
#include "touch_drv.h"

namespace
{
    enum class DemoState
    {
        TouchPage,
        CameraPreview,
        Halted
    };

    DemoState demoState = DemoState::Halted;
    bool cameraPreviewReady = false;
    bool touchWasActive = false;

    bool startCameraPreview()
    {
        if (cameraPreviewReady)
        {
            return true;
        }

        Serial.println("[MAIN] Starting camera preview");

        if (!cameraInit())
        {
            Serial.println("[MAIN] Camera init failed");
            return false;
        }

        if (!cameraPrintSensorInfo())
        {
            Serial.println("[MAIN] Sensor info failed");
            return false;
        }

        if (!cameraCaptureOnce())
        {
            Serial.println("[MAIN] Single capture failed");
            return false;
        }

        if (!previewInit())
        {
            Serial.println("[MAIN] Preview init failed");
            return false;
        }

        cameraPreviewReady = true;
        return true;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("Touch + Preview demo");

    if (!displayInit())
    {
        Serial.println("[MAIN] Display init failed");
        return;
    }

    displayRunFullScreenColorTest();

    if (!touchInit())
    {
        Serial.println("[MAIN] Touch init failed");
        return;
    }

    displayPrepareTouchPreviewDemo();
    Serial.println("[MAIN] Touch page ready");
    demoState = DemoState::TouchPage;
}

void loop()
{
    if (demoState == DemoState::Halted)
    {
        delay(1000);
        return;
    }

    if (demoState == DemoState::CameraPreview)
    {
        previewLoop();
        return;
    }

    TouchPoint point = {};
    if (!touchRead(point))
    {
        delay(30);
        return;
    }

    displayShowTouchPoint(point.x, point.y, point.touched);

    if (!point.touched)
    {
        touchWasActive = false;
        delay(30);
        return;
    }

    if (touchWasActive)
    {
        delay(30);
        return;
    }

    touchWasActive = true;
    Serial.printf("[MAIN] Touch x=%u y=%u\n", point.x, point.y);

    if (!displayIsPreviewButtonPressed(point.x, point.y))
    {
        delay(30);
        return;
    }

    Serial.println("[MAIN] Preview button pressed");
    if (!startCameraPreview())
    {
        Serial.println("[MAIN] Preview start failed");
        demoState = DemoState::Halted;
        return;
    }

    Serial.println("[MAIN] Camera preview running");
    demoState = DemoState::CameraPreview;
}






