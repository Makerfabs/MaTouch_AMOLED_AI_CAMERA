#pragma once

#include <stddef.h>
#include <stdint.h>

#include "detection_result.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ai_engine_init(void);
esp_err_t ai_engine_detect_rgb565(const uint8_t *rgb565_data,
                                  size_t rgb565_len,
                                  int width,
                                  int height,
                                  detection_result_t *result);

#ifdef __cplusplus
}
#endif
