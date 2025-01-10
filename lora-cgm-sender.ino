#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TimeLib.h>
#include <Time.h>
#include <ArduinoJson.h>
#include "credentials.h"
#include <Crypto.h>
#include <ChaCha.h>
#include <SPI.h>
#include <LoRa.h>
#include <LoRaCrypto.h>
#include <LoRaCryptoCreds.h>
#include <ExpirationTimer.h>
#include "propane-tank.h"
#include "thermometer.h"

#define ENABLE_LORA_SENDER
#define ENABLE_LORA_RECEIVER
#define ENABLE_DISPLAY

#if defined(ENABLE_LORA_SENDER) || defined(ENABLE_LORA_RECEIVER)
#define ENABLE_LORA
uint16_t deviceId = 0;
#endif

#if defined(ENABLE_LORA)
struct clockInfo_struct {
  time_t time;
  time_t dstBegins;
  time_t dstEnds;
};

struct cgm_struct {
  uint16_t mgPerDl;
  time_t time;
};

LoRaCrypto* loRaCrypto;

LoRaClass FspiLoRa;
// #define FspiLoRa LoRa
uint setupState = 0x00;
#endif

#if defined(ENABLE_LORA_SENDER)
void sendPacket(uint16_t messageType, byte* data, uint dataLength) {
  // FspiLoRa.idle();
  FspiLoRa.beginPacket();

  byte encryptedMessage[255];
  uint encryptedMessageLength;
  uint32_t counter = loRaCrypto->encrypt(encryptedMessage, &encryptedMessageLength, deviceId, messageType, data, dataLength);
  FspiLoRa.write(encryptedMessage, encryptedMessageLength);

  Serial.print("Sending packet: ");
  Serial.print(counter);
  Serial.print(", type = ");
  Serial.print(messageType);
  Serial.print(", length = ");
  Serial.println(encryptedMessageLength);

  FspiLoRa.endPacket();

  delay(500);  // Wait for the message to transmit

  FspiLoRa.receive();

  // FspiLoRa.sleep();
}
#endif

#if defined(ENABLE_DISPLAY)
// #define DISPLAY_TYPE_LCD_042
#define DISPLAY_TYPE_TFT
// #define DISPLAY_TYPE_ST7735_128_160
#define DISPLAY_TYPE_ILI9488_480_320

#if defined(DISPLAY_TYPE_LCD_042)
#include <U8g2lib.h>
#include <Wire.h>
#define SDA_PIN 5
#define SCL_PIN 6
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);   // EastRising 0.42" OLED
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
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
#endif

#define PROPANE_TIMEOUT (3600 * 6)
#define TEMPERATURE_TIMEOUT 300
#endif

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

String token;
long tokenExpires = 0;
bool callApi(const char* endpoint, const char* requestType, JsonDocument* doc) {
  char url[255];
  bool doPost = false;
  char postData[128];

  WiFiClientSecure client;
  HTTPClient https;
  client.setInsecure();

  strncpy(url, endpoint, sizeof(url));
  if (strcmp(requestType, "propane") == 0) {
    String authorization = "Basic " + String(PROPANE_CREDENTIALS_BASE64);
    https.addHeader("Authorization", authorization);
  } else if (strcmp(requestType, "temperature") == 0) {
    strncat(url, OPEN_WEATHER_MAP_API_KEY, sizeof(url));
  } else if (memcmp(requestType, "cgm", 3) == 0) {
    bool performLogin = (memcmp(&requestType[3], "Login", 5) == 0);

    // Serial.println("[HTTPS] begin...");
    https.addHeader("Content-Type", "application/json");
    https.addHeader("product", "llu.android");
    https.addHeader("version", "4.9.0");
    https.addHeader("user-agent", "curl/8.4.0");
    https.addHeader("accept", "*/*");
    if (performLogin) {
      sprintf(postData, "{\"email\":\"%s\",\"password\":\"%s\"}", CGM_USERNAME, CGM_PASSWORD);
      doPost = true;
    } else {
      String authorization = "Bearer " + token;
      https.addHeader("Authorization", authorization);
    }
  }

  if (!https.begin(client, url)) {  // HTTPS
    Serial.println("[HTTPS] Unable to connect");
    https.end();
    return false;
  }

  int httpCode;
  if (doPost) {
    // Serial.println("[HTTPS] POST...");
    httpCode = https.POST(postData);
  } else {
    // Serial.println("[HTTPS] GET...");
    httpCode = https.GET();
  }

  // httpCode will be negative on error
  if (httpCode <= 0) {
    Serial.printf("[HTTPS] request failed, error: %s\n", https.errorToString(httpCode).c_str());
    https.end();
    return false;
  }

  if ((httpCode != HTTP_CODE_OK) &&
      (httpCode != HTTP_CODE_MOVED_PERMANENTLY)) {
    Serial.printf("[HTTPS] response code: %d\n", httpCode);
    Serial.println("[HTTPS] request not processed due to response code");
    https.end();
    return false;
  }

  String payload = https.getString();
  // Serial.println(payload);  // Print the response body
  deserializeJson(*doc, payload);

  https.end();

  return true;
}

#if defined(ENABLE_DISPLAY)
void drawBorder(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color) {
  float wd = 6.0;
  float radius = wd / 2.0;

  tft.drawWideLine(x + radius, y + radius, w - radius, y + radius, wd, color);
  tft.drawWideLine(x + radius, y + radius, x + radius, h - radius, wd, color);
  tft.drawWideLine(w - radius, y + radius, w - radius, h - radius, wd, color);
  tft.drawWideLine(x + radius, h - radius, w - radius, h - radius, wd, color);
}
#endif

volatile long mgPerDl = -1;
volatile int propaneLevel = -1;
volatile double temperature = -100.0;

#if defined(DATA_COLLECTOR)
long httpsTaskHighWaterMark = LONG_MAX;
ExpirationTimer propaneExpirationTimer = ExpirationTimer();
ExpirationTimer temperatureExpirationTimer = ExpirationTimer();
void vHttpsTask(void* pvParameters) {
  while (true) {
    JsonDocument doc;

    if (time(nullptr) > tokenExpires) {
      if (callApi("https://api.libreview.io/llu/auth/login", "cgmLogin", &doc)) {
        token = (const char*) doc["data"]["authTicket"]["token"];
        tokenExpires = (long) doc["data"]["authTicket"]["expires"];

        struct tm timeinfo;
        gmtime_r((const time_t*) &tokenExpires, &timeinfo);
        Serial.print("Auth token expires at ");
        Serial.println(asctime(&timeinfo));
      }
    }

    if (time(nullptr) < tokenExpires) {
      if (callApi("https://api.libreview.io/llu/connections", "cgmNologin", &doc)) {
        JsonObject connection = doc["data"][0];
        mgPerDl = (long) connection["glucoseMeasurement"]["ValueInMgPerDl"];
        // mgPerDl = 888;
        const char* timestamp = (const char*) connection["glucoseMeasurement"]["Timestamp"];
        Serial.print("Glucose level = ");
        Serial.print(mgPerDl);
        Serial.print(" mg/dL at ");
        Serial.println(timestamp);
      }
    } else {
      Serial.println("Auth token is outdated!");
      Serial.println("It should be automatically reauthorized the next time we attempt to fetch the data.");
      Serial.println("If not then the login credentials are bad");
    }

    if (propaneExpirationTimer.isExpired(PROPANE_TIMEOUT * 1000)) {
      if (callApi("https://ws.otodatanetwork.com/neevoapp/v1/DataService.svc/GetAllDisplayPropaneDevices", "propane", &doc)) {
        propaneLevel = (int) doc[0]["Level"];
        Serial.print("Propane level = ");
        Serial.print(propaneLevel);
        Serial.println("%");
      }

      propaneExpirationTimer.reset();
    }

    if (temperatureExpirationTimer.isExpired(TEMPERATURE_TIMEOUT * 1000)) {
      if (callApi("https://api.openweathermap.org/data/2.5/weather?lat=47.3874978&lon=-122.1391124&appid=", "temperature", &doc)) {
        temperature = (((double) doc["main"]["temp"] - 273.15) * (9/5)) + 32;
        Serial.print("Temperature = ");
        Serial.println(temperature);
      }

      temperatureExpirationTimer.reset();
    }

    if (uxTaskGetStackHighWaterMark(NULL) < httpsTaskHighWaterMark) {
      Serial.printf("httpsTaskHighWaterMark has changed from %d to %d\n", (httpsTaskHighWaterMark == LONG_MAX ? -1 : httpsTaskHighWaterMark), uxTaskGetStackHighWaterMark(NULL));
      httpsTaskHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    }

    vTaskDelay(60000);
  }
}
#endif

#if defined(ENABLE_LORA_SENDER)
void sendNetworkTime() {
  struct clockInfo_struct clockInfo;

  clockInfo.time = time(nullptr);
  clockInfo.dstBegins = dstBegins;
  clockInfo.dstEnds = dstEnds;

  sendPacket(1, (byte*) &clockInfo, sizeof(clockInfo));  // Time update
}

long oldSendCgmMgPerDl = -1;
ExpirationTimer cgmGuaranteeTimer;
void sendCgmData(long mgPerDl, bool forceUpdate) {
  if ((mgPerDl != oldSendCgmMgPerDl) ||
      cgmGuaranteeTimer.isExpired(600000) ||  // Once every ten minutes per openweathermap.com
      forceUpdate) {
    struct cgm_struct cgm = { mgPerDl & 0xFFFF, time(nullptr) };
    sendPacket(29, (byte*) &cgm, sizeof(cgm));  // CGM reading
    cgmGuaranteeTimer.reset();
    oldSendCgmMgPerDl = mgPerDl;
  }
}

int oldSendPropaneLevel = -1;
ExpirationTimer propaneGuaranteeTimer;
void sendPropaneLevel(int propaneLevel, bool forceUpdate) {
  if ((propaneLevel != oldSendPropaneLevel) ||
      cgmGuaranteeTimer.isExpired(3600000) ||  // Once per hour
      forceUpdate) {
    byte data = (propaneLevel >= 0 ? propaneLevel & 0xFF : 0xFF);
    sendPacket(30, (byte*) &data, sizeof(data));  // Propane level in percent
    propaneGuaranteeTimer.reset();
    oldSendPropaneLevel = propaneLevel;
  }
}
#endif

void rightJustify(const char* displayBuffer,
                  uint8_t font,
                  uint8_t fontSize,
                  uint32_t color,
                  int16_t baseX,
                  int16_t baseY,
                  int16_t textWidth) {
  tft.setTextSize(fontSize);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(textWidth);
  tft.drawString(displayBuffer, baseX, baseY, font);
  tft.setTextDatum(TL_DATUM);
}

#if defined(ENABLE_DISPLAY)
long oldDisplayMgPerDl = -1;
void displayCgmData(long mgPerDl) {
  char displayBuffer[8];

  if (mgPerDl != oldDisplayMgPerDl) {
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
    drawBorder(0, 0, tft.width(), tft.height(), color);

    sprintf(displayBuffer, "%d", mgPerDl);
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE, color, 462, 9, 3 * 96);
#endif
    oldDisplayMgPerDl = mgPerDl;
  }
}

char oldDisplayTime[8] = {'\0'};
void displayClock() {
  char displayBuffer[8];
  time_t nowSecs = time(nullptr);
  struct tm timeinfo;
  gmtime_r((const time_t *) &nowSecs, &timeinfo);

  int hour = timeinfo.tm_hour + (timezone / 3600);
  if (hour < 0) {
    hour += 24;
  }
  sprintf(displayBuffer, "%2d:%02d", hour, timeinfo.tm_min);

  if (strcmp(displayBuffer, oldDisplayTime) != 0) {
    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));
#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_CLOCK, TFT_GREEN, 142, 75, 4.5 * 96);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_CLOCK, TFT_GREEN, 462, 160, 4.5 * 96);
#endif
    strcpy(oldDisplayTime, displayBuffer);
  }
}

int oldDisplayPropaneLevel = -1;
void displayPropaneLevel() {
  if (propaneLevel != oldDisplayPropaneLevel) {
    char displayBuffer[8];

    tft.pushImage(20, 11, 64, 64, PROPANE_TANK);
    sprintf(displayBuffer, "%d", propaneLevel);
#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 29, 2 * 16);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 20, 2 * 16);
#endif

    oldDisplayPropaneLevel = propaneLevel;
  }
}

double oldDisplayTemperature = -100.0;
void displayTemperature() {
  if (temperature != oldDisplayTemperature) {
    char displayBuffer[8];

    tft.pushImage(17, 80, 64, 64, THERMOMETER);
    sprintf(displayBuffer, "%3.0f", temperature);
#if defined(DISPLAY_TYPE_ST7735_128_160)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 89, 2 * 16);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    rightJustify(displayBuffer, FONT_NUMBER, FONT_SIZE_PROPANE, TFT_GREEN, 160, 89, 2 * 16);
#endif

    oldDisplayTemperature = temperature;
  }
}

#endif

#if defined(ENABLE_LORA_RECEIVER)
void receiveLoRaData() {
  // try to parse encrypted message
  int encryptedMessageSize = FspiLoRa.parsePacket();
  // Serial.println(encryptedMessageSize);
  if (!encryptedMessageSize) {
    return;
  }

  // received an encrypted message
  Serial.print("Received message, size = ");
  Serial.print(encryptedMessageSize);
  // print RSSI of message
  Serial.print(" with RSSI ");
  Serial.print(FspiLoRa.packetRssi());

  // read encrypted message
  byte encryptedMessage[255];
  uint encryptedMessageLength = 0;
  while (FspiLoRa.available()) {
    encryptedMessage[encryptedMessageLength++] = FspiLoRa.read();
  }

  byte data[encryptedMessageLength];
  MessageMetadata messageMetadata;
  uint decryptStatus = loRaCrypto->decrypt(data, encryptedMessage, encryptedMessageLength, &messageMetadata);
  if (decryptStatus != LoRaCryptoDecryptErrors::DECRYPT_OK) {
    char message[255];
    loRaCrypto->decryptErrorMessage(decryptStatus, message);
    Serial.print(" (");
    Serial.print(message);
    Serial.println(")");

    return;
  }

  char displayBuffer[255];
  sprintf(displayBuffer, ", device id = %d, message type = %d, ", messageMetadata.deviceId, messageMetadata.type);
  Serial.print(displayBuffer);

  switch (messageMetadata.type) {
    // Network time
    case 1:
      struct clockInfo_struct clockInfo;
      memcpy(&clockInfo, data, sizeof(clockInfo));

      struct timeval tv;
      tv.tv_sec = clockInfo.time;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      dstBegins = clockInfo.dstBegins;
      dstEnds = clockInfo.dstEnds;

      break;

    // Boot-sync
    case 2:
#if defined(ENABLE_LORA_SENDER)
      {
        sprintf(displayBuffer, "\"boot-sync messageId %d with deviceId = %d at time %" PRId64 "\"", messageMetadata.counter, *((uint16_t*) data), time(nullptr));
        Serial.println(displayBuffer);

        sendNetworkTime();
        sendCgmData(mgPerDl, true);
        sendPropaneLevel(propaneLevel, true);
      }
#endif
      break;

    case 29:
      {
        struct cgm_struct cgm;

        memcpy(&cgm, data, sizeof(cgm));

        mgPerDl = cgm.mgPerDl;
        sprintf(displayBuffer, "\"messageId %d with cgm reading = %d at time %" PRId64 "\"", messageMetadata.counter, cgm.mgPerDl, cgm.time);
        Serial.println(displayBuffer);
      }
      break;

    case 30:
      {
        sprintf(displayBuffer, "\"messageId %d with propane reading = %d at time %" PRId64 "\"", messageMetadata.counter, (int) data[0], time(nullptr));
        Serial.println(displayBuffer);
        propaneLevel = (int) data[0];
      }
      break;

    default:
      sprintf(displayBuffer, "unknown message type %d", messageMetadata.type);
      Serial.println(displayBuffer);
  }
}
#endif

#if defined(ENABLE_LORA)
SPIClass spi2(FSPI);
#endif
void setup() {
#if defined(ENABLE_DISPLAY)
#if defined(DISPLAY_TYPE_LCD_042)
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
#elif defined(DISPLAY_TYPE_TFT)
  tft.init();
  // tft.init(INITR_BLACKTAB);

  #if defined(DISPLAY_TYPE_ST7735_128_160)
  tft.setRotation(3);
  #else
  tft.setRotation(1);
  #endif
  tft.setTextWrap(false, false);
  tft.fillScreen(TFT_BLACK);
  drawBorder(0, 0, tft.width(), tft.height(), TFT_GREEN);
  tft.setTextSize(1);
  tft.setCursor(5, 8, FONT_NUMBER_2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.println(" Waiting...");
#endif

  // Turn on backlight LED for ILI9488
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
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

  #if defined(ENABLE_LORA)
  // spi2.begin(SCK, MISO, MOSI, SS); // ESP32-C3-Zero
  // FspiLoRa.setPins(7, 8, 3);  // ESP32-Zero-RFM95W (C3)
  spi2.begin(5, 6, 7, 8); // ESP32-S3-Zero
  FspiLoRa.setSPI(spi2);
  // MISO;
  // MOSI;
  // SCK;
  // SS;
  
  // FspiLoRa.setPins(ss, reset, dio0);
  // FspiLoRa.setPins(7, 9, 18);  // ESP32 C3 dev board
  // FspiLoRa.setPins(8, 9, 10);  // Pico
  // FspiLoRa.setPins(8, 4, 3);  // Feather M0 LoRa
  FspiLoRa.setPins(8, 9, 4);  // ESP32-Zero-RFM95W (S3)
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(4, INPUT);

  if (!FspiLoRa.begin(912900000)) {
    Serial.println("Starting LoRa failed! Waiting 60 seconds for restart...");
    delay(60000);
    Serial.println("Restarting!");
    ESP.restart();
    // while (1);
  }

  FspiLoRa.setSpreadingFactor(10);
  FspiLoRa.setSignalBandwidth(125000);
  FspiLoRa.setCodingRate4(5);
  FspiLoRa.setPreambleLength(8);
  FspiLoRa.setSyncWord(0x12);
  FspiLoRa.enableCrc();

  Serial.println("LoRa started successfully");

  loRaCrypto = new LoRaCrypto(&encryptionCredentials);
#endif

  // FspiLoRa.idle();
#if defined(LORA_ENABLE_RECEIVER)
  FspiLoRa.receive();
#endif

#if defined(DATA_COLLECTOR)
  propaneExpirationTimer.forceExpired();
  temperatureExpirationTimer.forceExpired();
#endif

  setupState = 0x00;
}

struct deviceMapping_struct {
  const char* macAddress;
  uint16_t deviceId;
};

struct deviceMapping_struct deviceMapping[] = {
  {"24:58:7c:dc:99:d0", 32},  // Large display #1
  {"00:00:00:00:00:00", 33},  // Small display
  {"34:b7:da:59:0a:90", 34}  // Large display #2
};

unsigned long baseMillis = millis();
void loop() {
  switch (setupState) {
    // Initialize the time
    case 0x00:
      {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        Serial.print("Connecting to Wi-Fi");
#if defined(ENABLE_DISPLAY)
        tft.println(" Connecting to Wi-Fi...");
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

#if defined(ENABLE_LORA)
        uint8_t baseMac[6];
        char macAddress[18];
        esp_err_t result = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
        if (result != ESP_OK) {
          Serial.println("Failed to read MAC address");
        }

        sprintf(macAddress, "%02x:%02x:%02x:%02x:%02x:%02x",
                baseMac[0], baseMac[1], baseMac[2],
                baseMac[3], baseMac[4], baseMac[5]);
        Serial.print("MAC address = ");
        Serial.println(macAddress);

        uint deviceMappingCount = sizeof(deviceMapping) / sizeof(struct deviceMapping_struct);
        uint i;
        for (i = 0; i < deviceMappingCount; i++) {
          if (strcmp(macAddress, deviceMapping[i].macAddress) == 0) {
            deviceId = deviceMapping[i].deviceId;
            Serial.print("Device ID = ");
            Serial.println(deviceId);
            break;
          }
        }

        if (i == deviceMappingCount) {
          Serial.println("There is no matching device!!! Using device ID 0");
        }
#endif

        setupState = 0x01;
      }

      break;

    // Initialize NTP
    case 0x01:
      {
        Serial.print(F("Waiting for NTP time sync..."));
#if defined(ENABLE_DISPLAY)
        tft.println("Waiting for NTP time sync...");
#endif

        setClock();

#if defined(ENABLE_DISPLAY)
        tft.fillScreen(TFT_BLACK);
        drawBorder(0, 0, tft.width(), tft.height(), TFT_GREEN);
#endif

        Serial.print(F("Broadcasting boot-sync message for device ID = "));
        Serial.println(deviceId);
        sendPacket(2, (byte*) &deviceId, sizeof(deviceId));  // Boot-sync message

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
#if defined(ENABLE_LORA_SENDER)
      sendCgmData(mgPerDl, false);
      sendPropaneLevel(propaneLevel, false);
#endif

#if defined(ENABLE_DISPLAY)
      displayCgmData(mgPerDl);
      displayClock();
      displayPropaneLevel();
      displayTemperature();
#endif

#if defined(ENABLE_LORA_RECEIVER)
      // Serial.println("ENABLE_LORA_RECEIVER");
      receiveLoRaData();
#endif

      break;
  }

  taskYIELD();
}
