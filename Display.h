#pragma once

#include "lora-cgm-sender.ino.globals.h"
#include "data.h"

// #define DISPLAY_TYPE_LCD_042
#define DISPLAY_TYPE_TFT
// #define DISPLAY_TYPE_ST7735_128_160
#define DISPLAY_TYPE_ILI9488_480_320


#if defined(DISPLAY_TYPE_LCD_042)
#include <U8g2lib.h>
#include <Wire.h>
#elif defined(DISPLAY_TYPE_TFT)
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#endif

class Display {
  private:
    volatile struct data_struct* _data;
    struct data_struct* _oldData;
#if defined(DISPLAY_TYPE_LCD_042)
    U8G2_SSD1306_72X40_ER_F_HW_I2C* _u8g2;
#elif defined(DISPLAY_TYPE_TFT)
    TFT_eSPI* _tft;
#endif
    bool _initializeDisplay;

    void _displayClock();
    void _displayCgmData();
    void _displayPropaneLevel();
    void _displayTemperature();

  public:
    Display(volatile struct data_struct* data);
    ~Display();

    void setup();
    void loop();

    void print(const char* val);
    void println(const char* val);
    void resetDisplay();
};