#include <Arduino.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <Time.h>
#include <ExpirationTimer.h>

#include "lora-cgm-sender.ino.globals.h"

#include "data.h"
volatile struct data_struct data = {0, -1, -1, -100.0};

#if defined(ENABLE_DISPLAY)
#include "Display.h"
Display* display;
#endif

#if defined(DATA_COLLECTOR)
#include "dataCollector.h"
#endif

#if defined(ENABLE_SYNC)
#include "Sync.h"
Sync* syncX;
SPIClass spi2(FSPI);
#endif

uint setupState = 0x00;

// Setting the clock...
volatile int timezone = -(8 * 3600);
volatile time_t dstBegins = -1;
volatile time_t dstEnds = -1;
void setClock() {
  configTime(0, 0, "pool.ntp.org");

  time_t nowSecs = time(nullptr);
  while (nowSecs < -(timezone * 2)) {
    delay(500);
    Serial.print(F("."));
    taskYIELD();
    nowSecs = time(nullptr);
  }

  Serial.println();
  struct tm timeinfo;
  gmtime_r((const time_t *) &nowSecs, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

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
  // spi2.begin(SCK, MISO, MOSI, SS); // ESP32-C3-Zero
  spi2.begin(5, 6, 7, 8); // ESP32-S3-Zero
  syncX = new Sync(&data, &spi2);
  syncX->setup();
#endif

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  setupState = 0x00;
}

unsigned long baseMillis = millis();
void loop() {
  switch (setupState) {
    // Initialize the time
    case 0x00:
      {

#if defined(DATA_COLLECTOR)
        Serial.print("Connecting to Wi-Fi");
#if defined(ENABLE_DISPLAY)
        display->println(" Connecting to Wi-Fi...");
#endif
        baseMillis = millis();
        while ((WiFi.status() != WL_CONNECTED) &&
              ((millis() - baseMillis) < 10000)) {
          Serial.print(".");
          delay(1000);
          taskYIELD();
        }
        Serial.println();
        Serial.print("Connected with IP: ");
        Serial.println(WiFi.localIP());
        Serial.println();
#endif

        setupState = 0x01;
      }

      break;

    // Initialize NTP
    case 0x01:
      {
#if defined(DATA_COLLECTOR)
        Serial.print(F("Waiting for NTP time sync..."));
#if defined(ENABLE_DISPLAY)
        display->println("Waiting for NTP time sync...");
#endif

        setClock();

#if defined(ENABLE_DISPLAY)
        display->resetDisplay();
#endif

        Serial.print(F("Broadcasting boot-sync message for device ID = "));
        uint16_t deviceId = syncX->deviceId();
        Serial.println(deviceId);
        // sendPacket(2, (byte*) &deviceId, sizeof(deviceId));  // Boot-sync message
#endif

        setupState = 0x02;
      }

      break;

    // Initialize the HTTPS thread
    case 0x02:
      {
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
      data.time = time(nullptr);

#if defined(ENABLE_SYNC)
      syncX->loop();
#endif

#if defined(ENABLE_DISPLAY)
      display->loop();
#endif

      break;
  }

  taskYIELD();
}
