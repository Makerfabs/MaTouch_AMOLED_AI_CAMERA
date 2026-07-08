#pragma once

#include <stdbool.h>

#define DETECTION_RESULT_MAX_ANIMALS 3

typedef struct {
    char class_name[24];
    float confidence;
    int box[4];
} detected_animal_t;

typedef struct {
    bool detected;
    int image_width;
    int image_height;
    int animal_count;
    detected_animal_t animals[DETECTION_RESULT_MAX_ANIMALS];
} detection_result_t;

#ifdef __cplusplus
extern "C" {
#endif

void detection_result_set_none(detection_result_t *result, int image_width, int image_height);

#ifdef __cplusplus
}
#endif
