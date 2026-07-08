#include "co5300_driver.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LCD_HOST          SPI2_HOST
#define LCD_H_RES         368
#define LCD_V_RES         448
#define LCD_X_GAP         16
#define LCD_Y_GAP         0
#define LCD_RST_GPIO      -1
#define LCD_CS_GPIO       42
#define LCD_PCLK_GPIO     40
#define LCD_DATA0_GPIO    38
#define LCD_DATA1_GPIO    39
#define LCD_DATA2_GPIO    13
#define LCD_DATA3_GPIO    21
#define LCD_TE_GPIO       41
#define LCD_VCI_EN_GPIO   14
#define LCD_PCLK_HZ           (40 * 1000 * 1000)
#define LCD_DRAW_LINES        64
#define LCD_DMA_BUFFER_COUNT  2
#define LCD_TE_TIMEOUT_MS     20

static esp_lcd_panel_handle_t s_panel_handle;
static SemaphoreHandle_t s_color_trans_done;
static SemaphoreHandle_t s_te_signal;
static uint16_t *s_color_buffers[LCD_DMA_BUFFER_COUNT];

static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 10},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 0},
    {0x63, (uint8_t []){0xFF}, 1, 0},
    {0x29, (uint8_t []){0x00}, 0, 0},
    {0x51, (uint8_t []){0xD0}, 1, 0},
    {0x58, (uint8_t []){0x00}, 1, 10},
};

IRAM_ATTR static bool co5300_notify_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;

    xSemaphoreGiveFromISR(s_color_trans_done, &need_yield);
    return need_yield == pdTRUE;
}

IRAM_ATTR static void co5300_te_isr_handler(void *arg)
{
    BaseType_t need_yield = pdFALSE;

    if (s_te_signal) {
        xSemaphoreGiveFromISR(s_te_signal, &need_yield);
    }
    if (need_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void co5300_wait_te(void)
{
    if (s_te_signal) {
        xSemaphoreTake(s_te_signal, 0);
        xSemaphoreTake(s_te_signal, pdMS_TO_TICKS(LCD_TE_TIMEOUT_MS));
    }
}

static void co5300_init_te(void)
{
    s_te_signal = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_te_signal ? ESP_OK : ESP_ERR_NO_MEM);

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LCD_TE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(LCD_TE_GPIO, co5300_te_isr_handler, NULL));
}

static void co5300_enable_power(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LCD_VCI_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(LCD_VCI_EN_GPIO, 1));
    vTaskDelay(pdMS_TO_TICKS(100));
}

void co5300_driver_init(void)
{
    co5300_enable_power();
    co5300_init_te();

    spi_bus_config_t bus_config = CO5300_PANEL_BUS_QSPI_CONFIG(
        LCD_PCLK_GPIO,
        LCD_DATA0_GPIO,
        LCD_DATA1_GPIO,
        LCD_DATA2_GPIO,
        LCD_DATA3_GPIO,
        LCD_H_RES * LCD_DRAW_LINES * sizeof(uint16_t)
    );
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    s_color_trans_done = xSemaphoreCreateCounting(LCD_DMA_BUFFER_COUNT, 0);
    ESP_ERROR_CHECK(s_color_trans_done ? ESP_OK : ESP_ERR_NO_MEM);
    for (int i = 0; i < LCD_DMA_BUFFER_COUNT; i++) {
        s_color_buffers[i] = heap_caps_malloc(LCD_H_RES * LCD_DRAW_LINES * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_ERROR_CHECK(s_color_buffers[i] ? ESP_OK : ESP_ERR_NO_MEM);
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS_GPIO, co5300_notify_color_trans_done, NULL);
    io_config.pclk_hz = LCD_PCLK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    co5300_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(co5300_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel_handle, LCD_X_GAP, LCD_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));
}

void co5300_driver_fill_color(uint16_t color)
{
    uint16_t spi_color = SPI_SWAP_DATA_TX(color, 16);

    uint16_t *color_buffer = s_color_buffers[0];

    for (int i = 0; i < LCD_H_RES * LCD_DRAW_LINES; i++) {
        color_buffer[i] = spi_color;
    }

    for (int y = 0; y < LCD_V_RES; y += LCD_DRAW_LINES) {
        int y_end = y + LCD_DRAW_LINES;
        if (y_end > LCD_V_RES) {
            y_end = LCD_V_RES;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel_handle,
                                                  0,
                                                  y,
                                                  LCD_H_RES,
                                                  y_end,
                                                  color_buffer));
        xSemaphoreTake(s_color_trans_done, portMAX_DELAY);
    }
}

void co5300_driver_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    int width = x_end - x_start;
    int height = y_end - y_start;
    const uint16_t *pixels = (const uint16_t *)color_data;

    if (x_start == 0 && y_start == 0 && x_end == LCD_H_RES && y_end == LCD_V_RES) {
        co5300_wait_te();
    }

    int submitted_transfers = 0;
    int pending_transfers = 0;
    for (int y = 0; y < height; y += LCD_DRAW_LINES) {
        int lines = height - y;
        if (lines > LCD_DRAW_LINES) {
            lines = LCD_DRAW_LINES;
        }

        if (pending_transfers >= LCD_DMA_BUFFER_COUNT) {
            xSemaphoreTake(s_color_trans_done, portMAX_DELAY);
            pending_transfers--;
        }

        int buffer_index = submitted_transfers % LCD_DMA_BUFFER_COUNT;
        uint16_t *color_buffer = s_color_buffers[buffer_index];
        memcpy(color_buffer, pixels + y * width, width * lines * sizeof(uint16_t));
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel_handle,
                                                  x_start,
                                                  y_start + y,
                                                  x_end,
                                                  y_start + y + lines,
                                                  color_buffer));
        submitted_transfers++;
        pending_transfers++;
    }

    while (pending_transfers > 0) {
        xSemaphoreTake(s_color_trans_done, portMAX_DELAY);
        pending_transfers--;
    }
}

int co5300_driver_get_width(void)
{
    return LCD_H_RES;
}

int co5300_driver_get_height(void)
{
    return LCD_V_RES;
}
