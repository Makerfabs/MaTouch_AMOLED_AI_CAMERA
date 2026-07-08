#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "display_drv.h"

namespace
{
    Arduino_DataBus *bus = new Arduino_ESP32QSPI(
        LCD_CS,
        LCD_SCLK,
        LCD_SDIO0,
        LCD_SDIO1,
        LCD_SDIO2,
        LCD_SDIO3);

    Arduino_GFX *gfx = new Arduino_CO5300(
        bus,
        LCD_RST,
        0,
        false,
        LCD_WIDTH,
        LCD_HEIGHT,
        16,
        0,
        0,
        0);

    void drawStageTitle(const char *text)
    {
        gfx->setTextColor(BLACK);
        gfx->setTextSize(3);
        gfx->setCursor(36, 44);
        gfx->println(text);
    }

    constexpr int16_t kStage3StatusX = 40;
    constexpr int16_t kStage3StatusY = 248;
    constexpr int16_t kStage3StatusValueX = 88;
    constexpr int16_t kStage3StatusValueW = 180;
    constexpr int16_t kStage3StatusRowH = 28;
    constexpr int16_t kPreviewButtonX = 124;
    constexpr int16_t kPreviewButtonY = 308;
    constexpr int16_t kPreviewButtonW = 120;
    constexpr int16_t kPreviewButtonH = 120;

    void drawThickRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, int16_t thickness)
    {
        gfx->fillRect(x, y, w, thickness, color);
        gfx->fillRect(x, y + h - thickness, w, thickness, color);
        gfx->fillRect(x, y, thickness, h, color);
        gfx->fillRect(x + w - thickness, y, thickness, h, color);
    }
}

bool displayInit()
{
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);

    if (!gfx->begin())
    {
        Serial.println("[STAGE2] Display begin failed");
        return false;
    }

    Serial.println("[STAGE2] Display init passed");
    return true;
}

void displayRunFullScreenColorTest()
{
    Serial.println("[STAGE2] Running full-screen color test");

    gfx->fillScreen(RED);
    delay(600);
    gfx->fillScreen(GREEN);
    delay(600);
    gfx->fillScreen(BLUE);
    delay(600);
    gfx->fillScreen(WHITE);
    delay(600);
    gfx->fillScreen(BLACK);
    delay(600);

    Serial.println("[STAGE2] Full-screen color test finished");
}

void displayPrepareTouchPreviewDemo()
{
    gfx->fillScreen(WHITE);
    drawStageTitle("Touch + Preview");

    gfx->setTextColor(BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(32, 128);
    gfx->println("Touch screen to test coords");
    gfx->setCursor(32, 160);
    gfx->println("Press button to start camera");
    gfx->setCursor(32, 192);
    gfx->printf("Area: %d x %d\n", LCD_WIDTH, LCD_HEIGHT);

    gfx->setTextColor(BLACK, WHITE);
    gfx->fillRect(kStage3StatusValueX, kStage3StatusY, kStage3StatusValueW, kStage3StatusRowH, WHITE);
    gfx->fillRect(kStage3StatusValueX, kStage3StatusY + kStage3StatusRowH, kStage3StatusValueW, kStage3StatusRowH, WHITE);

    gfx->setCursor(kStage3StatusX, kStage3StatusY);
    gfx->println("X:");
    gfx->setCursor(kStage3StatusX, kStage3StatusY + kStage3StatusRowH);
    gfx->println("Y:");

    gfx->setCursor(kStage3StatusValueX, kStage3StatusY);
    gfx->println("-");
    gfx->setCursor(kStage3StatusValueX, kStage3StatusY + kStage3StatusRowH);
    gfx->println("-");

    gfx->fillRect(kPreviewButtonX, kPreviewButtonY, kPreviewButtonW, kPreviewButtonH, BLUE);
    gfx->drawRect(kPreviewButtonX, kPreviewButtonY, kPreviewButtonW, kPreviewButtonH, BLACK);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(kPreviewButtonX + 20, kPreviewButtonY + 48);
    gfx->println("button");
}

bool displayIsPreviewButtonPressed(uint16_t x, uint16_t y)
{
    return x >= kPreviewButtonX && x < (kPreviewButtonX + kPreviewButtonW) &&
           y >= kPreviewButtonY && y < (kPreviewButtonY + kPreviewButtonH);
}

void displayShowTouchPoint(uint16_t x, uint16_t y, bool touching)
{
    constexpr uint16_t backgroundColor = WHITE;

    gfx->setTextColor(BLACK, backgroundColor);
    gfx->setTextSize(2);

    gfx->fillRect(kStage3StatusValueX, kStage3StatusY, kStage3StatusValueW, kStage3StatusRowH, backgroundColor);
    gfx->fillRect(kStage3StatusValueX, kStage3StatusY + kStage3StatusRowH, kStage3StatusValueW, kStage3StatusRowH, backgroundColor);

    if (!touching)
    {
        gfx->setCursor(kStage3StatusValueX, kStage3StatusY);
        gfx->println("-");
        gfx->setCursor(kStage3StatusValueX, kStage3StatusY + kStage3StatusRowH);
        gfx->println("-");
        return;
    }

    if (x >= LCD_WIDTH)
    {
        x = LCD_WIDTH - 1;
    }

    if (y >= LCD_HEIGHT)
    {
        y = LCD_HEIGHT - 1;
    }

    gfx->setCursor(kStage3StatusValueX, kStage3StatusY);
    gfx->printf("%u\n", x);
    gfx->setCursor(kStage3StatusValueX, kStage3StatusY + kStage3StatusRowH);
    gfx->printf("%u\n", y);
}

void displayDrawRgb565Bitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h)
{
    if (bitmap == nullptr || w <= 0 || h <= 0)
    {
        return;
    }

    gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
}
