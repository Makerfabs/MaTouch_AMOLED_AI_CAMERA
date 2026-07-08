#include "camera_driver.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOARD_CAM_XCLK     4
#define BOARD_CAM_PCLK     7
#define BOARD_CAM_VSYNC    1
#define BOARD_CAM_HSYNC    2
#define BOARD_CAM_D0       9
#define BOARD_CAM_D1       11
#define BOARD_CAM_D2       12
#define BOARD_CAM_D3       10
#define BOARD_CAM_D4       8
#define BOARD_CAM_D5       6
#define BOARD_CAM_D6       5
#define BOARD_CAM_D7       3
#define BOARD_CAM_POWER_EN 46
#define BOARD_CAM_RST      -1
#define BOARD_CAM_SIOC     18
#define BOARD_CAM_SIOD     17
#define BOARD_CAM_PWDN     -1

#define CAMERA_POWER_EN_LEVEL 0
#define CAMERA_XCLK_FREQ_HZ   20000000

static const char *TAG = "camera_driver";

static void camera_enable_power(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_CAM_POWER_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_CAM_POWER_EN, CAMERA_POWER_EN_LEVEL));
    vTaskDelay(pdMS_TO_TICKS(300));
}

esp_err_t camera_driver_init(void)
{
    camera_enable_power();

    camera_config_t camera_config = {
        .pin_pwdn = BOARD_CAM_PWDN,
        .pin_reset = BOARD_CAM_RST,
        .pin_xclk = BOARD_CAM_XCLK,
        .pin_sccb_sda = BOARD_CAM_SIOD,
        .pin_sccb_scl = BOARD_CAM_SIOC,
        .pin_d7 = BOARD_CAM_D7,
        .pin_d6 = BOARD_CAM_D6,
        .pin_d5 = BOARD_CAM_D5,
        .pin_d4 = BOARD_CAM_D4,
        .pin_d3 = BOARD_CAM_D3,
        .pin_d2 = BOARD_CAM_D2,
        .pin_d1 = BOARD_CAM_D1,
        .pin_d0 = BOARD_CAM_D0,
        .pin_vsync = BOARD_CAM_VSYNC,
        .pin_href = BOARD_CAM_HSYNC,
        .pin_pclk = BOARD_CAM_PCLK,
        .xclk_freq_hz = CAMERA_XCLK_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t ret = esp_camera_init(&camera_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "check OV3660 power enable level, SCCB pins, XCLK, reset/pwdn pins and FPC direction");
        return ret;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        ESP_LOGI(TAG, "camera sensor pid=0x%04x", sensor->id.PID);
        sensor->set_vflip(sensor, 0);
        sensor->set_brightness(sensor, 1);
        sensor->set_saturation(sensor, -2);
    }

    ESP_LOGI(TAG, "OV3660 camera init done");
    return ESP_OK;
}

camera_fb_t *camera_driver_capture(void)
{
    return esp_camera_fb_get();
}

void camera_driver_return(camera_fb_t *frame_buffer)
{
    if (frame_buffer) {
        esp_camera_fb_return(frame_buffer);
    }
}
