#include <Arduino.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <Time.h>
#include <ExpirationTimer.h>

#include "lora-cgm-sender.ino.globals.h"

#include "data.h"
volatile struct data_struct data = {
  -1,  // time
  0,  // DST begin
  0,  // DST end
  0,  // standard time offset
  0,  // daylight time offset
  false,  // forceDisplayTimeUpdate
  false,  // forceLoRaTimeUpdate
  UNKNOWN_MG_PER_DL,  // CGM reading
  UNKNOWN_PROPANE_LEVEL,  // propane level
  UNKNOWN_TEMPERATURE,  // indoor temperature
  UNKNOWN_HUMIDITY,  // indoor humidity
  UNKNOWN_TEMPERATURE,  // outdoor temperature
  UNKNOWN_HUMIDITY  // outdoor humidity
};

#if defined(ENABLE_DISPLAY)
#include "Display.h"
Display* display;
#endif

#if defined(DATA_COLLECTOR)
#include "dataCollector.h"
#endif

#if defined(ENABLE_SYNC)
#include "LoRaSync.h"
LoRaSync* loRaSync;
SPIClass spi2(FSPI);
#endif

uint setupState = 0x00;

void setup() {
#if defined(ENABLE_DISPLAY)
  display = new Display(&data);
  display->setup();
#endif

  Serial.begin(9600);
  unsigned long baseMillis = millis();
  while (!Serial &&
         ((millis() - baseMillis) < 5000)) {
    taskYIELD();
  }

  Serial.println();
  Serial.println();
  Serial.println();
  Serial.println("Starting...");

#if defined(ENABLE_SYNC)
  // spi2.begin(SCK, MISO, MOSI, SS);
  spi2.begin(5, 6, 7, 8); // ESP32-S3-Zero
  loRaSync = new LoRaSync(&data, &spi2);
  loRaSync->setup();
#endif

#if defined(DATA_COLLECTOR)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

  setupState = 0x00;
}

unsigned long baseMillis = millis();
void loop() {
  switch (setupState) {
    // Initialize the time
    case 0x00:
      {
#if defined(DATA_COLLECTOR)
        Serial.print("Connecting to Wi-Fi...");
#if defined(ENABLE_DISPLAY)
        display->println(" Connecting to Wi-Fi...");
#endif
        baseMillis = millis();
#endif

        setupState = 0x01;
      }

      break;
    
    case 0x01:
      if ((WiFi.status() == WL_CONNECTED) ||
          ((millis() - baseMillis) > 10000)) {
        setupState = 0x02;
      }

      break;

    // Initialize the HTTPS thread
    case 0x02:
      {
        Serial.println();
        Serial.print("Connected with IP: ");
        Serial.println(WiFi.localIP());
        Serial.println();

#if defined(ENABLE_DISPLAY)
        display->resetDisplay();
#endif

#if defined(DATA_COLLECTOR)
        BaseType_t xReturned;
        TaskHandle_t xHandle = NULL;

        #define STACK_SIZE 16384  // 16KB
        xReturned = xTaskCreate(
          vHttpsTask,         /* Function that implements the task. */
          "HTTPS",           /* Text name for the task. */
          STACK_SIZE,        /* Stack size in words, not bytes. */
          NULL,              /* Parameter passed into the task. */
          5, // tskIDLE_PRIORITY,  /* Priority at which the task is created. */
          &xHandle);         /* Used to pass out the created task's handle. */

        if (xReturned != pdPASS) {
          Serial.println("HttpsTask could not be created");
        }
#endif

        setupState = 0xFF;
      }

      break;

    // Normal running
    case 0xFF:
      if (time(nullptr) > 86400 * 365) {  // Give NTP one year to sync
        data.time = time(nullptr);
      }

#if defined(ENABLE_SYNC)
      loRaSync->loop();
#endif

#if defined(ENABLE_DISPLAY)
      display->loop();
#endif

      break;
  }

  taskYIELD();
}
