#include "camera_lcd_app.h"

#include <stdlib.h>
#include <string.h>

#include "ai_engine.h"
#include "camera_driver.h"
#include "co5300_driver.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "font5x7.h"

#define AI_INFER_INTERVAL_MS 300
#define AI_RESULT_TTL_MS     2500
#define AI_TASK_STACK_SIZE   16384
#define AI_FRAME_MAX_SIZE    (320 * 240 * 2)
#define LCD_FRAME_BUFFER_SIZE (368 * 448 * 2)
#define LCD_MAX_WIDTH         368
#define OVERLAY_BAR_HEIGHT    24
#define OVERLAY_TEXT_SCALE   2
#define COLOR_BLACK          0x0000
#define COLOR_YELLOW         0xffe0
#define COLOR_GREEN          0x07e0

static const char *TAG = "camera_lcd_app";

static SemaphoreHandle_t s_ai_mutex;
static uint8_t *s_ai_frame_buffer;
static size_t s_ai_frame_len;
static int s_ai_frame_width;
static int s_ai_frame_height;
static int64_t s_ai_frame_time_ms;
static bool s_ai_frame_pending;
static detection_result_t s_detection_result;
static int64_t s_detection_result_time_ms;
static char s_ai_status[24] = "AI WAIT";
static uint16_t *s_lcd_frame_buffer;

static uint16_t to_spi_color(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static void camera_lcd_get_result_label(const detection_result_t *result, char *label, size_t label_size)
{
    if (!result || !result->detected || result->animal_count == 0) {
        snprintf(label, label_size, "NO ANIMAL");
        return;
    }

    const detected_animal_t *animal = &result->animals[0];
    int percent = (int)(animal->confidence * 100.0f + 0.5f);
    snprintf(label, label_size, "%s %d%%", animal->class_name, percent);
}

static void camera_lcd_blend_text_bar(uint16_t *buffer,
                                      int buffer_width,
                                      int y_start,
                                      int lines,
                                      const char *label,
                                      uint16_t text_color)
{
    uint16_t black = to_spi_color(COLOR_BLACK);
    uint16_t color = to_spi_color(text_color);

    for (int line = 0; line < lines; line++) {
        int screen_y = y_start + line;
        if (screen_y >= OVERLAY_BAR_HEIGHT) {
            continue;
        }
        for (int x = 0; x < buffer_width; x++) {
            buffer[line * buffer_width + x] = black;
        }
    }

    int text_x = 64;
    int text_y = 5;
    for (int i = 0; label[i] != '\0'; i++) {
        int char_x = text_x + i * 6 * OVERLAY_TEXT_SCALE;
        for (int col = 0; col < 5; col++) {
            uint8_t bits = font5x7_get(label[i], col);
            for (int row = 0; row < 7; row++) {
                if ((bits & (1 << row)) == 0) {
                    continue;
                }
                for (int sy = 0; sy < OVERLAY_TEXT_SCALE; sy++) {
                    int pixel_y = text_y + row * OVERLAY_TEXT_SCALE + sy;
                    if (pixel_y < y_start || pixel_y >= y_start + lines) {
                        continue;
                    }
                    for (int sx = 0; sx < OVERLAY_TEXT_SCALE; sx++) {
                        int pixel_x = char_x + col * OVERLAY_TEXT_SCALE + sx;
                        if (pixel_x >= 0 && pixel_x < buffer_width) {
                            buffer[(pixel_y - y_start) * buffer_width + pixel_x] = color;
                        }
                    }
                }
            }
        }
    }
}

static void camera_lcd_blend_detection_boxes(uint16_t *buffer,
                                             int buffer_width,
                                             int y_start,
                                             int lines,
                                             const detection_result_t *result,
                                             int dst_width,
                                             int dst_height,
                                             int source_width,
                                             int source_height)
{
    if (!result || !result->detected || result->animal_count == 0 || source_width <= 0 || source_height <= 0) {
        return;
    }

    uint16_t color = to_spi_color(COLOR_GREEN);
    for (int i = 0; i < result->animal_count; i++) {
        const detected_animal_t *animal = &result->animals[i];
        int x1 = animal->box[0] * dst_width / source_width;
        int y1 = animal->box[1] * dst_height / source_height;
        int x2 = animal->box[2] * dst_width / source_width;
        int y2 = animal->box[3] * dst_height / source_height;

        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        for (int y = y_start; y < y_start + lines; y++) {
            bool on_border_y = (y >= y1 && y < y1 + 2) || (y >= y2 - 2 && y < y2);
            bool within_y = y >= y1 && y < y2;
            if (!within_y) {
                continue;
            }

            for (int x = x1; x < x2; x++) {
                bool on_border_x = (x >= x1 && x < x1 + 2) || (x >= x2 - 2 && x < x2);
                if ((on_border_x || on_border_y) && x >= 0 && x < buffer_width) {
                    buffer[(y - y_start) * buffer_width + x] = color;
                }
            }
        }
    }
}

static void camera_lcd_draw_scaled_rgb565(const camera_fb_t *frame_buffer,
                                          int dst_x,
                                          int dst_y,
                                          int dst_width,
                                          int dst_height,
                                          const detection_result_t *result)
{
    if (!s_lcd_frame_buffer) {
        return;
    }

    const uint16_t *src_pixels = (const uint16_t *)frame_buffer->buf;
    int src_width = frame_buffer->width;
    int src_height = frame_buffer->height;
    char result_label[40];

    camera_lcd_get_result_label(result, result_label, sizeof(result_label));

    for (int y = 0; y < dst_height; y++) {
        int src_y = y * src_height / dst_height;
        uint16_t *dst_line = s_lcd_frame_buffer + y * dst_width;
        const uint16_t *src_line = src_pixels + src_y * src_width;

        for (int x = 0; x < dst_width; x++) {
            int src_x = x * src_width / dst_width;
            dst_line[x] = src_line[src_x];
        }
    }

    camera_lcd_blend_text_bar(s_lcd_frame_buffer,
                              dst_width,
                              0,
                              dst_height,
                              result_label,
                              result && result->detected ? COLOR_GREEN : COLOR_YELLOW);
    camera_lcd_blend_detection_boxes(s_lcd_frame_buffer,
                                     dst_width,
                                     0,
                                     dst_height,
                                     result,
                                     dst_width,
                                     dst_height,
                                     result && result->image_width > 0 ? result->image_width : src_width,
                                     result && result->image_height > 0 ? result->image_height : src_height);

    co5300_driver_draw_bitmap(dst_x,
                              dst_y,
                              dst_x + dst_width,
                              dst_y + dst_height,
                              s_lcd_frame_buffer);
}

static void camera_lcd_set_ai_status(const char *status)
{
    if (xSemaphoreTake(s_ai_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        strlcpy(s_ai_status, status, sizeof(s_ai_status));
        xSemaphoreGive(s_ai_mutex);
    }
}

static void camera_lcd_get_ai_snapshot(detection_result_t *result, char *status, size_t status_size)
{
    if (xSemaphoreTake(s_ai_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        *result = s_detection_result;
        if (esp_timer_get_time() / 1000 - s_detection_result_time_ms > AI_RESULT_TTL_MS) {
            result->detected = false;
            result->animal_count = 0;
        }
        strlcpy(status, s_ai_status, status_size);
        xSemaphoreGive(s_ai_mutex);
    }
}

static void camera_lcd_submit_ai_frame(camera_fb_t *frame_buffer)
{
    if (!s_ai_frame_buffer || frame_buffer->len > AI_FRAME_MAX_SIZE) {
        return;
    }

    if (xSemaphoreTake(s_ai_mutex, 0) == pdTRUE) {
        memcpy(s_ai_frame_buffer, frame_buffer->buf, frame_buffer->len);
        s_ai_frame_len = frame_buffer->len;
        s_ai_frame_width = frame_buffer->width;
        s_ai_frame_height = frame_buffer->height;
        s_ai_frame_time_ms = esp_timer_get_time() / 1000;
        s_ai_frame_pending = true;
        xSemaphoreGive(s_ai_mutex);
    }
}

static void camera_lcd_ai_task(void *arg)
{
    uint8_t *local_frame = heap_caps_malloc(AI_FRAME_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!local_frame) {
        ESP_LOGE(TAG, "local AI frame buffer alloc failed");
        camera_lcd_set_ai_status("AI MEM ERR");
        vTaskDelete(NULL);
    }

    while (1) {
        size_t frame_len = 0;
        int frame_width = 0;
        int frame_height = 0;

        if (xSemaphoreTake(s_ai_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_ai_frame_pending) {
                memcpy(local_frame, s_ai_frame_buffer, s_ai_frame_len);
                frame_len = s_ai_frame_len;
                frame_width = s_ai_frame_width;
                frame_height = s_ai_frame_height;
                s_ai_frame_pending = false;
                strlcpy(s_ai_status, "AI RUN", sizeof(s_ai_status));
            }
            xSemaphoreGive(s_ai_mutex);
        }

        if (frame_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        detection_result_t result;
        esp_err_t ret = ai_engine_detect_rgb565(local_frame, frame_len, frame_width, frame_height, &result);
        int64_t response_time_ms = esp_timer_get_time() / 1000;

        if (xSemaphoreTake(s_ai_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (ret == ESP_OK) {
                if (result.image_width == 0) {
                    result.image_width = frame_width;
                }
                if (result.image_height == 0) {
                    result.image_height = frame_height;
                }
                s_detection_result = result;
                s_detection_result_time_ms = response_time_ms;
                strlcpy(s_ai_status, "AI OK", sizeof(s_ai_status));
            } else {
                ESP_LOGW(TAG, "local AI detect failed: %s", esp_err_to_name(ret));
                detection_result_set_none(&s_detection_result, frame_width, frame_height);
                s_detection_result_time_ms = response_time_ms;
                strlcpy(s_ai_status, "AI ERR", sizeof(s_ai_status));
            }
            xSemaphoreGive(s_ai_mutex);
        }
    }
}

void camera_lcd_app_start(void)
{
    co5300_driver_init();
    co5300_driver_fill_color(0x0000);

    s_ai_mutex = xSemaphoreCreateMutex();
    s_ai_frame_buffer = heap_caps_malloc(AI_FRAME_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_lcd_frame_buffer = heap_caps_malloc(LCD_FRAME_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    detection_result_set_none(&s_detection_result, 320, 240);
    if (!s_ai_mutex || !s_ai_frame_buffer) {
        ESP_LOGE(TAG, "AI frame buffer alloc failed");
    }
    if (!s_lcd_frame_buffer) {
        ESP_LOGE(TAG, "LCD frame buffer alloc failed");
    }

    esp_err_t ai_ret = ai_engine_init();
    camera_lcd_set_ai_status(ai_ret == ESP_OK ? "AI WAIT" : "AI OFF");

    if (camera_driver_init() != ESP_OK) {
        while (1) {
            ESP_LOGE(TAG, "camera init failed, preview stopped");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    xTaskCreate(camera_lcd_ai_task,
                "ai_upload",
                AI_TASK_STACK_SIZE,
                NULL,
                4,
                NULL);

    detection_result_t detection_result;
    char ai_status[24];
    int64_t last_upload_ms = 0;

    while (1) {
        camera_fb_t *frame_buffer = camera_driver_capture();
        if (!frame_buffer) {
            ESP_LOGE(TAG, "capture failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (frame_buffer->format != PIXFORMAT_RGB565) {
            ESP_LOGE(TAG, "unsupported frame format=%d", frame_buffer->format);
            camera_driver_return(frame_buffer);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int lcd_width = co5300_driver_get_width();
        int lcd_height = co5300_driver_get_height();
        int draw_width = lcd_width;
        int draw_height = lcd_height;
        int x_start = 0;
        int y_start = 0;

        camera_lcd_get_ai_snapshot(&detection_result, ai_status, sizeof(ai_status));
        camera_lcd_draw_scaled_rgb565(frame_buffer, x_start, y_start, draw_width, draw_height, &detection_result);

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_upload_ms >= AI_INFER_INTERVAL_MS) {
            last_upload_ms = now_ms;
            camera_lcd_submit_ai_frame(frame_buffer);
        }

        camera_driver_return(frame_buffer);
    }
}
