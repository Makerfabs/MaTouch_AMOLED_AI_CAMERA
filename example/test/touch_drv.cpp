#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "touch_drv.h"

namespace
{
    constexpr uint8_t kTouchAddress = 0x15;
    constexpr uint8_t kRegTouchData = 0x02;

    bool readRegisters(uint8_t reg, uint8_t *buffer, size_t length)
    {
        Wire.beginTransmission(kTouchAddress);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0)
        {
            return false;
        }

        size_t received = Wire.requestFrom(static_cast<int>(kTouchAddress), static_cast<int>(length));
        if (received != length)
        {
            return false;
        }

        for (size_t index = 0; index < length; ++index)
        {
            buffer[index] = static_cast<uint8_t>(Wire.read());
        }

        return true;
    }
}

bool touchInit()
{
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(10);
    digitalWrite(TP_RST, HIGH);
    delay(50);

    pinMode(TP_INT, INPUT);

    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    uint8_t buffer[6] = {0};
    if (!readRegisters(kRegTouchData, buffer, sizeof(buffer)))
    {
        Serial.println("[STAGE3] Touch probe failed");
        return false;
    }

    Serial.println("[STAGE3] Touch init passed");
    return true;
}

bool touchRead(TouchPoint &point)
{
    uint8_t buffer[6] = {0};
    if (!readRegisters(kRegTouchData, buffer, sizeof(buffer)))
    {
        return false;
    }

    uint8_t touchCount = buffer[0] & 0x0F;
    point.touched = touchCount > 0;

    if (!point.touched)
    {
        point.x = 0;
        point.y = 0;
        return true;
    }

    point.x = static_cast<uint16_t>(((buffer[1] & 0x0F) << 8) | buffer[2]);
    point.y = static_cast<uint16_t>(((buffer[3] & 0x0F) << 8) | buffer[4]);
    return true;
}
