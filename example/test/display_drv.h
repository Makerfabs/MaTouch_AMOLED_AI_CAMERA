#pragma once

#include <Arduino.h>

bool displayInit();
void displayRunFullScreenColorTest();
void displayPrepareTouchPreviewDemo();
bool displayIsPreviewButtonPressed(uint16_t x, uint16_t y);
void displayShowTouchPoint(uint16_t x, uint16_t y, bool touching);
void displayDrawRgb565Bitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h);
