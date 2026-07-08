#include "camera_lcd_app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAMERA_APP_TASK_STACK_SIZE 16384
#define CAMERA_APP_TASK_PRIORITY   5

static void camera_app_task(void *arg)
{
    camera_lcd_app_start();
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreate(camera_app_task,
                "camera_app",
                CAMERA_APP_TASK_STACK_SIZE,
                NULL,
                CAMERA_APP_TASK_PRIORITY,
                NULL);
}
