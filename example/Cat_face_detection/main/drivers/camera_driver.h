#pragma once

#include "esp_camera.h"
#include "esp_err.h"

esp_err_t camera_driver_init(void);
camera_fb_t *camera_driver_capture(void);
void camera_driver_return(camera_fb_t *frame_buffer);
