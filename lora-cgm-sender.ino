#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "credentials.h"
#include <Crypto.h>
#include <ChaCha.h>
#include <SPI.h>
#include <LoRa.h>
#include <LoRaCrypto.h>
#include <LoRaCryptoCreds.h>

#define ENABLE_LORA
#define ENABLE_LORA_SENDER
// #define ENABLE_LORA_RECEIVER
#define DEVICE_ID 33
#define ENABLE_DISPLAY

#if defined(ENABLE_LORA)
LoRaCrypto* loRaCrypto;

LoRaClass FspiLoRa;
// #define FspiLoRa LoRa
uint setupState = 0x00;

void sendPacket(uint16_t messageType, byte* data, uint dataLength) {
  // FspiLoRa.idle();
  FspiLoRa.beginPacket();

  byte encryptedMessage[255];
  uint encryptedMessageLength;
  uint32_t counter = loRaCrypto->encrypt(encryptedMessage, &encryptedMessageLength, DEVICE_ID, messageType, data, dataLength);
  FspiLoRa.write(encryptedMessage, encryptedMessageLength);

  Serial.print("Sending packet: ");
  Serial.print(counter);
  Serial.print(", type = ");
  Serial.print(messageType);
  Serial.print(", length = ");
  Serial.println(encryptedMessageLength);

  FspiLoRa.endPacket();
  // delay(2000);
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
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
#endif
#endif

// Setting the clock...
void setClock() {
  configTime(0, 0, "pool.ntp.org");

  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2) {
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
bool callApi(const char* endpoint, bool performLogin, JsonDocument* doc) {
  WiFiClientSecure client;
  HTTPClient https;
  client.setInsecure();

  // Serial.println("[HTTPS] begin...");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("product", "llu.android");
  https.addHeader("version", "4.9.0");
  https.addHeader("user-agent", "curl/8.4.0");
  https.addHeader("accept", "*/*");
  if (!performLogin) {
    String authorization = "Bearer " + token;
    https.addHeader("Authorization", authorization);
  }

  if (!https.begin(client, endpoint)) {  // HTTPS
    Serial.println("[HTTPS] Unable to connect");
    https.end();
    return false;
  }

  int httpCode;
  if (performLogin) {
    // Serial.println("[HTTPS] POST...");
    char cgmCredentials[128];
    sprintf(cgmCredentials, "{\"email\":\"%s\",\"password\":\"%s\"}", CGM_USERNAME, CGM_PASSWORD);
    httpCode = https.POST(cgmCredentials);
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
  // https.writeToStream(&Serial);  // Print the response body
  deserializeJson(*doc, payload);

  https.end();

  return true;
}

void drawBorder(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color) {
  float wd = 6.0;
  float radius = wd / 2.0;

  tft.drawWideLine(x + radius, y + radius, w - radius, y + radius, wd, color);
  tft.drawWideLine(x + radius, y + radius, x + radius, h - radius, wd, color);
  tft.drawWideLine(w - radius, y + radius, w - radius, h - radius, wd, color);
  tft.drawWideLine(x + radius, h - radius, w - radius, h - radius, wd, color);
}

long httpsTaskHighWaterMark = LONG_MAX;
volatile long mgPerDl = -1;
void vHttpsTask(void* pvParameters) {
  while (true) {
    JsonDocument doc;

    if (time(nullptr) > tokenExpires) {
      if (callApi("https://api.libreview.io/llu/auth/login", true, &doc)) {
        token = (const char*) doc["data"]["authTicket"]["token"];
        tokenExpires = (long) doc["data"]["authTicket"]["expires"];

        struct tm timeinfo;
        gmtime_r((const time_t*) &tokenExpires, &timeinfo);
        Serial.print("Auth token expires at ");
        Serial.println(asctime(&timeinfo));
      }
    }

    if (time(nullptr) < tokenExpires) {
      if (callApi("https://api.libreview.io/llu/connections", false, &doc)) {
        JsonObject connection = doc["data"][0];
        mgPerDl = (long) connection["glucoseMeasurement"]["ValueInMgPerDl"];
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

    if (uxTaskGetStackHighWaterMark(NULL) < httpsTaskHighWaterMark) {
      Serial.printf("httpsTaskHighWaterMark has changed from %d to %d\n", (httpsTaskHighWaterMark == LONG_MAX ? -1 : httpsTaskHighWaterMark), uxTaskGetStackHighWaterMark(NULL));
      httpsTaskHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    }

    vTaskDelay(60000);
  }
}

#if defined(ENABLE_LORA_SENDER)
struct cgm_struct {
  uint16_t mgPerDl;
};

uint loRaGuaranteeTimer = millis();
void sendCgmData(long mgPerDl, long oldLoRaMgPerDl) {
  if ((mgPerDl != oldLoRaMgPerDl) ||
      ((millis() - loRaGuaranteeTimer) > 300000)) {
    struct cgm_struct cgm = { mgPerDl & 0xFFFF };
    sendPacket(29, (byte*) &cgm, sizeof(cgm));  // CGM reading
    loRaGuaranteeTimer = millis();
  }
}
#endif

#if defined(ENABLE_DISPLAY)
void displayCgmData(long mgPerDl, long oldLoRaMgPerDl) {
  char displayBuffer[255];

  Serial.print("mgPerDl = ");
  Serial.print(mgPerDl);
  Serial.print(", oldLoRaMgPerDl = ");
  Serial.println(oldLoRaMgPerDl);
  delay(5000);
  if (mgPerDl != oldLoRaMgPerDl) {
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
    sprintf(displayBuffer, " %d", mgPerDl);
    // tft.fillScreen(TFT_BLACK);
    // strcpy(oldTime, "");
    drawBorder(0, 0, tft.width(), tft.height(), color);
    tft.setCursor(0, 4, 4);
    tft.setTextColor(color);
#if defined(DISPLAY_TYPE_ST7735_128_160)
    tft.setTextSize(3);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    tft.setTextSize(6);
#endif
    tft.println(displayBuffer);
#endif
  }
}

char oldTime[255] = {'\0'};
void displayClock() {
  char displayBuffer[255];
  time_t nowSecs = time(nullptr);
  struct tm timeinfo;
  gmtime_r((const time_t *) &nowSecs, &timeinfo);

  int hour = timeinfo.tm_hour - 7;
  if (hour < 0) {
    hour += 24;
  }
  sprintf(displayBuffer, " %02d:%02d", hour, timeinfo.tm_min);

  if (strcmp(displayBuffer, oldTime) != 0) {
    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));

    tft.setCursor(0, 160, 4);
    tft.setTextColor(TFT_BLACK);
#if defined(DISPLAY_TYPE_ST7735_128_160)
    tft.setTextSize(3);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    tft.setTextSize(6);
#endif
    tft.println(oldTime);
    strcpy(oldTime, displayBuffer);

    // tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 160, 4);
    tft.setTextColor(TFT_GREEN);
#if defined(DISPLAY_TYPE_ST7735_128_160)
    tft.setTextSize(3);
#elif defined(DISPLAY_TYPE_ILI9488_480_320)
    tft.setTextSize(6);
#endif
    tft.println(displayBuffer);
  }
}
#endif

#if defined(ENABLE_LORA_RECEIVER)
void onReceive(int packetSize) {
  Serial.println("onReceive");
}

void receiveLoRaData() {
  // try to parse encrypted message
  int encryptedMessageSize = LoRa.parsePacket();
  // Serial.println(encryptedMessageSize);
  if (!encryptedMessageSize) {
    return;
  }

  // received an encrypted message
  Serial.print("Received message, size = ");
  Serial.print(encryptedMessageSize);
  // print RSSI of message
  Serial.print(" with RSSI ");
  Serial.print(LoRa.packetRssi());

  // read encrypted message
  byte encryptedMessage[255];
  uint encryptedMessageLength = 0;
  while (LoRa.available()) {
    encryptedMessage[encryptedMessageLength++] = LoRa.read();
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

  sprintf(displayBuffer, ", device id = %d, message type = %d, ", messageMetadata.deviceId, messageMetadata.type);
  Serial.print(displayBuffer);

  switch (messageMetadata.type) {
    case 29:
      sprintf(displayBuffer, "\"hello %d with hasMail = %d\"", messageMetadata.counter, data[0]);
      Serial.println(displayBuffer);
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
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  drawBorder(0, 0, tft.width(), tft.height(), TFT_GREEN);
  tft.setCursor(4, 8, 4);
  tft.setTextColor(TFT_GREEN);
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

  #if defined(ENABLE_LORA)
  // spi2.begin(SCK, MISO, MOSI, SS); // ESP32-C3-Zero
  spi2.begin(5, 6, 7, 8); // ESP32-S3-Zero
  FspiLoRa.setSPI(spi2);
  // MISO;
  // MOSI;
  // SCK;
  // SS;
  
  // FspiLoRa.setPins(ss, reset, dio0);
  // FspiLoRa.setPins(7, 8, 3);  // ESP32-Zero-RFM95W (C3)
  FspiLoRa.setPins(8, 9, 4);  // ESP32-Zero-RFM95W (S3)
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

  FspiLoRa.idle();

  loRaCrypto = new LoRaCrypto(&encryptionCredentials);
#endif

  setupState = 0x00;
}

long oldLoRaMgPerDl = -1;
unsigned long baseMillis = millis();
void loop() {
  switch (setupState) {
    // Initialize the time
    case 0x00:
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

      Serial.print("Connecting to Wi-Fi");
      tft.println(" Connecting to Wi-Fi...");
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

      setupState = 0x01;

      break;

    // Initialize NTP
    case 0x01:
      Serial.print(F("Waiting for NTP time sync..."));
      tft.println("Waiting for NTP time sync...");

      setClock();

      tft.fillScreen(TFT_BLACK);
      drawBorder(0, 0, tft.width(), tft.height(), TFT_GREEN);

      setupState = 0x02;

      break;

    // Initialize the HTTPS thread
    case 0x02:
      {
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

        setupState = 0xFF;
      }

      break;

    // Normal running
    case 0xFF:
#if defined(ENABLE_LORA_SENDER)
      sendCgmData(mgPerDl, oldLoRaMgPerDl);
#endif

#if defined(ENABLE_DISPLAY)
      displayCgmData(mgPerDl, oldLoRaMgPerDl);
      displayClock();
#endif

#if defined(ENABLE_LORA_RECEIVER)
      receiveLoRaData();
#endif

      break;
  }

  oldLoRaMgPerDl = mgPerDl;
  taskYIELD();
}
