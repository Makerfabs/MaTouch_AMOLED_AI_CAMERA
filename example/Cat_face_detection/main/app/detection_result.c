#include "detection_result.h"

#include <string.h>

void detection_result_set_none(detection_result_t *result, int image_width, int image_height)
{
    if (!result) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->image_width = image_width;
    result->image_height = image_height;
}
