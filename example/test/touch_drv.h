#pragma once

#include <Arduino.h>

struct TouchPoint
{
    bool touched;
    uint16_t x;
    uint16_t y;
};

bool touchInit();
bool touchRead(TouchPoint &point);
