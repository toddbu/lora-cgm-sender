#include <Arduino.h>
#include <TimeLib.h>
#include <Time.h>
#include <SPI.h>
#include "credentials.h"
#include "Display.h"
#include "propane-tank.h"
#include "thermometer.h"

#if defined(DISPLAY_TYPE_LCD_042)
#define SDA_PIN 5
#define SCL_PIN 6

#elif defined(DISPLAY_TYPE_TFT)
#define FONT_NUMBER 7
#define FONT_NUMBER_2 2
#if defined(DISPLAY_TYPE_ST7735_128_160)
#define FONT_SIZE 2
#define FONT_SIZE_CLOCK 1
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
#define FONT_SIZE 3
#define FONT_SIZE_CLOCK 3
#endif
#define FONT_SIZE_PROPANE 1
#endif

void drawBorder(TFT_eSPI* tft, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color) {
  float wd = 6.0;
  float radius = wd / 2.0;

  tft->drawWideLine(x + radius, y + radius, w - radius, y + radius, wd, color);
  tft->drawWideLine(x + radius, y + radius, x + radius, h - radius, wd, color);
  tft->drawWideLine(w - radius, y + radius, w - radius, h - radius, wd, color);
  tft->drawWideLine(x + radius, h - radius, w - radius, h - radius, wd, color);
}

void rightJustify(TFT_eSPI* tft,
                  const char* displayBuffer,
                  uint8_t font,
                  uint8_t fontSize,
                  uint32_t color,
                  int16_t baseX,
                  int16_t baseY,
                  int16_t textWidth) {
  tft->setTextSize(fontSize);
  tft->setTextColor(color, TFT_BLACK);
  tft->setTextDatum(TR_DATUM);
  tft->setTextPadding(textWidth);
  tft->drawString(displayBuffer, baseX, baseY, font);
  tft->setTextDatum(TL_DATUM);
}

Display::Display(volatile struct data_struct* data) {
  _data = data;
  _oldData = (struct data_struct*) malloc(sizeof(struct data_struct));
  memcpy(_oldData, (const void*) _data, sizeof(struct data_struct));
}

Display::~Display() {
  free(_oldData);

#if defined(DISPLAY_TYPE_LCD_042)
  free(_u8g2);
#elif defined(DISPLAY_TYPE_TFT)
    free(_tft);
#endif
}

void Display::setup() {
  #if defined(DISPLAY_TYPE_LCD_042)
  _u8g2 = new U8G2_SSD1306_72X40_ER_F_HW_I2C(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);   // EastRising 0.42" OLED
  #elif defined(DISPLAY_TYPE_TFT)
  _tft = new TFT_eSPI();  // Invoke custom library
  #endif

#if defined(DISPLAY_TYPE_LCD_042)
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
#elif defined(DISPLAY_TYPE_TFT)
  _tft->init();
  // _tft->init(INITR_BLACKTAB);

  #if defined(DISPLAY_TYPE_ST7735_128_160)
  _tft->setRotation(3);
  #else
  _tft->setRotation(1);
  #endif
  _tft->setTextWrap(false, false);
  _tft->fillScreen(TFT_BLACK);
  drawBorder(_tft, 0, 0, _tft->width(), _tft->height(), TFT_GREEN);

  _tft->setTextSize(1);
  _tft->setCursor(5, 8, FONT_NUMBER_2);
  _tft->setTextColor(TFT_GREEN, TFT_BLACK);
  _tft->println(" Waiting...");
#endif

  // Turn on backlight LED for ILI9488
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
}

void Display::loop() {
  _displayClock();
  _displayCgmData();
  _displayPropaneLevel();
  _displayTemperature();
};

void Display::print(const char* val) {
  _tft->print(val);
}

void Display::println(const char* val) {
  _tft->println(val);
}

void Display::resetDisplay() {
  _tft->fillScreen(TFT_BLACK);
  drawBorder(_tft, 0, 0, _tft->width(), _tft->height(), TFT_GREEN);
}

void Display::_displayClock() {
  if ((_data->time / 60) !=  (_oldData->time / 60)) {
    char displayBuffer[8];
    time_t nowSecs = _data->time;
    struct tm timeinfo;
    gmtime_r((const time_t *) &nowSecs, &timeinfo);

    const int timezone = -28800;
    int hour = timeinfo.tm_hour + (timezone / 3600);
    if (hour < 0) {
      hour += 24;
    }
    sprintf(displayBuffer, "%2d:%02d", hour, timeinfo.tm_min);

    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));
#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_CLOCK, TFT_GREEN, 142, 75, 4.5 * 96);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_CLOCK, TFT_GREEN, 462, 160, 4.5 * 96);
#endif
    _oldData->time = _data->time;
  }
}

void Display::_displayCgmData() {
  char displayBuffer[8];

  long mgPerDl = _data->mgPerDl;
  if (mgPerDl != _oldData->mgPerDl) {
#if defined(DISPLAY_TYPE_LCD_042)
    u8g2.clearBuffer();  // clear the internal memory
    u8g2.setFont(u8g_font_9x18);  // choose a suitable font
    sprintf(displayBuffer, "%d", mgPerDl);
    u8g2.drawStrX2(0, 20, displayBuffer);  // write something to the internal memory
    u8g2.sendBuffer();  // transfer internal memory to the display
#elif defined(DISPLAY_TYPE_TFT)
    uint32_t color;
    if ((mgPerDl < 70) ||
        (mgPerDl > 250)) {
      color = TFT_RED;
    } else if ((mgPerDl >= 70 && mgPerDl < 80) ||
                (mgPerDl > 150 && mgPerDl <= 250)) {
      color = TFT_YELLOW;
    } else {
      color = TFT_GREEN;
    }
    drawBorder(_tft, 0, 0, _tft->width(), _tft->height(), color);

    sprintf(displayBuffer, "%d", mgPerDl);
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE, color, 462, 9, 3 * 96);
#endif
    _oldData->mgPerDl = mgPerDl;
  }
}

void Display::_displayPropaneLevel() {
  if (_data->propaneLevel != _oldData->propaneLevel) {
    char displayBuffer[8];

    _tft->pushImage(20, 11, 64, 64, PROPANE_TANK);
    sprintf(displayBuffer, "%d", _data->propaneLevel);
#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 29, 2 * 16);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 20, 2 * 16);
#endif

    _oldData->propaneLevel = _data->propaneLevel;
  }
}

void Display::_displayTemperature() {
  if (_data->temperature != _oldData->temperature) {
    char displayBuffer[8];

    _tft->pushImage(17, 80, 64, 64, THERMOMETER);
    sprintf(displayBuffer, "%3.0f", _data->temperature);
#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 89, 2 * 16);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 89, 2 * 16);
#endif

    _oldData->temperature = _data->temperature;
  }
}