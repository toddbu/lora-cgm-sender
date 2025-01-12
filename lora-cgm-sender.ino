#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <TimeLib.h>
#include <Time.h>
#include "credentials.h"
#include <Crypto.h>
#include <ChaCha.h>
#include <SPI.h>
#include <LoRa.h>
#include <LoRaCrypto.h>
#include <LoRaCryptoCreds.h>
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

#if defined(ENABLE_LORA)
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

  byte messageData[encryptedMessageLength];
  MessageMetadata messageMetadata;
  uint decryptStatus = loRaCrypto->decrypt(messageData, encryptedMessage, encryptedMessageLength, &messageMetadata);
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
      memcpy(&clockInfo, messageData, sizeof(clockInfo));

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
        sprintf(displayBuffer, "\"boot-sync messageId %d with deviceId = %d at time %" PRId64 "\"", messageMetadata.counter, *((uint16_t*) messageData), time(nullptr));
        Serial.println(displayBuffer);

        sendNetworkTime();
        sendCgmData(data.mgPerDl, true);
        sendPropaneLevel(data.propaneLevel, true);
      }
#endif
      break;

    case 29:
      {
        struct cgm_struct cgm;

        memcpy(&cgm, messageData, sizeof(cgm));

        data.mgPerDl = cgm.mgPerDl;
        sprintf(displayBuffer, "\"messageId %d with cgm reading = %d at time %" PRId64 "\"", messageMetadata.counter, cgm.mgPerDl, cgm.time);
        Serial.println(displayBuffer);
      }
      break;

    case 30:
      {
        sprintf(displayBuffer, "\"messageId %d with propane reading = %d at time %" PRId64 "\"", messageMetadata.counter, (int) messageData[0], time(nullptr));
        Serial.println(displayBuffer);
        data.propaneLevel = (int) messageData[0];
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
        Serial.println(deviceId);
        sendPacket(2, (byte*) &deviceId, sizeof(deviceId));  // Boot-sync message
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

#if defined(ENABLE_LORA_SENDER)
      sendCgmData(data.mgPerDl, false);
      sendPropaneLevel(data.propaneLevel, false);
#endif

#if defined(ENABLE_DISPLAY)
      display->loop();
#endif

#if defined(ENABLE_LORA_RECEIVER)
      // Serial.println("ENABLE_LORA_RECEIVER");
      receiveLoRaData();
#endif

      break;
  }

  taskYIELD();
}
