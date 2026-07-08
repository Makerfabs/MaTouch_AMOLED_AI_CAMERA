#include "ai_engine.h"

#include <string.h>

#include <list>
#include <vector>

#include "cat_face_detect_mn03.hpp"
#include "dl_detect_define.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#define AI_ENGINE_SCORE_THRESHOLD 0.4f
#define AI_ENGINE_NMS_THRESHOLD   0.3f
#define AI_ENGINE_TOP_K           DETECTION_RESULT_MAX_ANIMALS
#define AI_ENGINE_RESIZE_SCALE    0.3f

static const char *TAG = "ai_engine";
static CatFaceDetectMN03 *s_cat_face_detector;

static int clamp_to_range(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

esp_err_t ai_engine_init(void)
{
    if (!s_cat_face_detector) {
        s_cat_face_detector = new CatFaceDetectMN03(AI_ENGINE_SCORE_THRESHOLD,
                                                   AI_ENGINE_NMS_THRESHOLD,
                                                   AI_ENGINE_TOP_K,
                                                   AI_ENGINE_RESIZE_SCALE);
        ESP_RETURN_ON_FALSE(s_cat_face_detector, ESP_ERR_NO_MEM, TAG, "cat face detector alloc failed");
    }

    ESP_LOGI(TAG, "ESP-DL cat face detector init done");
    return ESP_OK;
}

esp_err_t ai_engine_detect_rgb565(const uint8_t *rgb565_data,
                                  size_t rgb565_len,
                                  int width,
                                  int height,
                                  detection_result_t *result)
{
    ESP_RETURN_ON_FALSE(rgb565_data && result, ESP_ERR_INVALID_ARG, TAG, "invalid detect args");
    ESP_RETURN_ON_FALSE(width > 0 && height > 0, ESP_ERR_INVALID_ARG, TAG, "invalid frame size");
    ESP_RETURN_ON_FALSE(rgb565_len >= (size_t)width * height * sizeof(uint16_t), ESP_ERR_INVALID_SIZE, TAG, "invalid frame length");
    ESP_RETURN_ON_FALSE(s_cat_face_detector, ESP_ERR_INVALID_STATE, TAG, "cat face detector not initialized");

    int64_t start_us = esp_timer_get_time();
    detection_result_set_none(result, width, height);

    std::vector<int> input_shape = {height, width, 3};
    std::list<dl::detect::result_t> &predictions = s_cat_face_detector->infer((uint16_t *)rgb565_data, input_shape);

    for (std::list<dl::detect::result_t>::iterator prediction = predictions.begin();
         prediction != predictions.end() && result->animal_count < DETECTION_RESULT_MAX_ANIMALS;
         ++prediction) {
        if (prediction->score < AI_ENGINE_SCORE_THRESHOLD || prediction->box.size() < 4) {
            continue;
        }

        detected_animal_t *animal = &result->animals[result->animal_count];
        strlcpy(animal->class_name, "cat_face", sizeof(animal->class_name));
        animal->confidence = prediction->score;
        animal->box[0] = clamp_to_range(prediction->box[0], 0, width - 1);
        animal->box[1] = clamp_to_range(prediction->box[1], 0, height - 1);
        animal->box[2] = clamp_to_range(prediction->box[2], 0, width - 1);
        animal->box[3] = clamp_to_range(prediction->box[3], 0, height - 1);

        if (animal->box[2] <= animal->box[0] || animal->box[3] <= animal->box[1]) {
            continue;
        }

        result->animal_count++;
    }

    result->detected = result->animal_count > 0;
    ESP_LOGI(TAG,
             "cat face detect count=%d total=%lldms",
             result->animal_count,
             (esp_timer_get_time() - start_us) / 1000);

    return ESP_OK;
}
