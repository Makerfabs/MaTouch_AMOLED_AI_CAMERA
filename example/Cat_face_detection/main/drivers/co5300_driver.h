#pragma once

#include <stdint.h>

void co5300_driver_init(void);
void co5300_driver_fill_color(uint16_t color);
void co5300_driver_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data);
int co5300_driver_get_width(void);
int co5300_driver_get_height(void);
