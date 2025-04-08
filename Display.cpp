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
#define FONT_SIZE 1
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
  _initializeDisplay = true;
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

// Rotation values:
//   1: Display connections opposite MCU
//   3: Display connections on the same side as the MCU
  #if defined(DISPLAY_TYPE_ST7735_128_160)
  _tft->setRotation(3);
  #else
  _tft->setRotation(3);
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
#if defined(DISPLAY_TYPE_ILI9488_480_320)
  _displayPropaneLevel();
  _displayTemperature();
#endif

  _initializeDisplay = false;
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
  if (((_data->time / 60) !=  (_oldData->time / 60)) ||
      (_data->time < 0) ||
      _data->forceDisplayTimeUpdate) {
    char displayBuffer[8];
    if (_data->time > 0) {
      time_t nowSecs = _data->time;
      struct tm timeinfo;
      gmtime_r((const time_t *) &nowSecs, &timeinfo);
      // Serial.print("nowSecs = ");
      // Serial.println(nowSecs);
      // Serial.print("_data->dstBegin = ");
      // Serial.println(_data->dstBegin);
      // Serial.print("_data->dstEnd) = ");
      // Serial.println(_data->dstEnd);

      int32_t timezoneOffset;
      if ((nowSecs >= _data->dstBegin) &&
          (nowSecs < _data->dstEnd)) {
        timezoneOffset = _data->daylightTimezoneOffset;
      } else {
        timezoneOffset = _data->standardTimezoneOffset;
      }
      int32_t hour = timeinfo.tm_hour + (timezoneOffset / 3600);
      if (hour < 0) {
        hour += 24;
      }
      sprintf(displayBuffer, "%2d:%02d", hour, timeinfo.tm_min);

      Serial.print("Current time: ");
      Serial.println(asctime(&timeinfo));
    } else {
      strcpy(displayBuffer, "--:--");
    }
#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_CLOCK, TFT_GREEN, 150, 69, 5.75 * 24);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_CLOCK, TFT_GREEN, 462, 160, 4.5 * 96);
#endif
    _oldData->time = _data->time;
    _data->forceDisplayTimeUpdate = false;
  }
}

void Display::_displayCgmData() {
  char displayBuffer[8];

  ushort mgPerDl = _data->mgPerDl;
  if ((mgPerDl != _oldData->mgPerDl) ||
      _initializeDisplay) {
    // Serial.println("---");
    // Serial.println(mgPerDl);
    // Serial.println(UNKNOWN_MG_PER_DL);
    if (mgPerDl != UNKNOWN_MG_PER_DL) {
      sprintf(displayBuffer, "%d", mgPerDl);
    } else {
      strcpy(displayBuffer, "---");
    }

#if defined(DISPLAY_TYPE_LCD_042)
    u8g2.clearBuffer();  // clear the internal memory
    u8g2.setFont(u8g_font_9x18);  // choose a suitable font
    u8g2.drawStrX2(0, 20, displayBuffer);  // write something to the internal memory
    u8g2.sendBuffer();  // transfer internal memory to the display
#elif defined(DISPLAY_TYPE_TFT)
    uint32_t color;
    if (mgPerDl == UNKNOWN_MG_PER_DL) {
      color = TFT_GREEN;
    } else if ((mgPerDl < 70) ||
              (mgPerDl > 250)) {
      color = TFT_RED;
    } else if ((mgPerDl >= 70 && mgPerDl < 80) ||
                (mgPerDl > 150 && mgPerDl <= 250)) {
      color = TFT_YELLOW;
    } else {
      color = TFT_GREEN;
    }
    drawBorder(_tft, 0, 0, _tft->width(), _tft->height(), color);

#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE, color, 150, 13, 4.5 * 24);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE, color, 462, 9, 3 * 96);
#endif
#endif

    _oldData->mgPerDl = mgPerDl;
  }
}

#if defined(DISPLAY_TYPE_ILI9488_480_320)
void Display::_displayPropaneLevel() {
  char displayBuffer[8];

  if ((_data->propaneLevel != _oldData->propaneLevel) ||
      _initializeDisplay) {
    _tft->pushImage(20, 11, 64, 64, PROPANE_TANK);
    if (_data->propaneLevel != UNKNOWN_PROPANE_LEVEL) {
      sprintf(displayBuffer, "%d", _data->propaneLevel);
    } else {
      strcpy(displayBuffer, "--");
    }
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 20, 2 * 16);

    _oldData->propaneLevel = _data->propaneLevel;
  }
}

void Display::_displayTemperature() {
  char displayBuffer[8];

  if ((_data->outdoorTemperature != _oldData->outdoorTemperature) ||
      _initializeDisplay) {
    _tft->pushImage(19, 80, 64, 64, THERMOMETER);
    if (_data->outdoorTemperature != UNKNOWN_TEMPERATURE) {
      sprintf(displayBuffer, "%3.0f", _data->outdoorTemperature);
    } else {
      strcpy(displayBuffer, "--");
    }
    rightJustify(_tft, displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 89, 6 * 16);

    _oldData->outdoorTemperature = _data->outdoorTemperature;
  }
}
#endif
