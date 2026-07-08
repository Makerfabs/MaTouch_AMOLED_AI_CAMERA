#pragma once

#include "esp_camera.h"

bool cameraInit();
bool cameraPrintSensorInfo();
bool cameraCaptureOnce();
camera_fb_t *cameraCaptureFrame();
void cameraReleaseFrame(camera_fb_t *frame);
