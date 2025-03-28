#include <esp_wifi.h>
#include <Crypto.h>
#include <ChaCha.h>
#include "data.h"
#include "LoRaSync.h"

#include "lora-cgm-sender.ino.globals.h"

struct deviceMapping_struct {
  const char* macAddress;
  uint16_t deviceId;
};

struct deviceMapping_struct deviceMapping[] = {
  {"24:58:7c:dc:99:d0", 32},  // Large display #1
  {"24:58:7c:dc:8b:44", 33},  // Small display
  {"34:b7:da:59:0a:90", 34},  // Large display #2
};

struct clockInfo_struct {
  time_t time;
  time_t dstBegin;
  time_t dstEnd;
  int32_t standardTimezoneOffset;
  int32_t daylightTimezoneOffset;
};

struct cgm_struct {
  uint16_t mgPerDl;
  time_t time;
};

struct temperature_struct {
  float indoorTemperature;
  float outdoorTemperature;
  byte indoorHumidity;
  byte outdoorHumidity;
  byte padding0[2];
};

LoRaCrypto* loRaCrypto;

LoRaSync::LoRaSync(volatile struct data_struct* data, SPIClass* spi) {
  _data = data;
  _oldData = (struct data_struct*) malloc(sizeof(struct data_struct));
  memcpy(_oldData, (const void*) _data, sizeof(struct data_struct));

  _spi = spi;
  _loRa = new LoRaClass();
}

LoRaSync::~LoRaSync() {
  delete _loRa;

  free(_oldData);
}

void LoRaSync::setup() {
  _loRa->setSPI(*_spi);
  
  // _loRa->setPins(ss, reset, dio0);
  // _loRa->setPins(7, 9, 18);  // ESP32 C3 dev board
  // _loRa->setPins(8, 9, 10);  // Pico
  // _loRa->setPins(8, 4, 3);  // Feather M0 LoRa
  _loRa->setPins(8, 9, 4);  // ESP32-Zero-RFM95W (S3)
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(4, INPUT);

  uint8_t baseMac[6];
  char macAddress[20];
  wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t result = esp_wifi_init(&wifiInitConfig);
  Serial.print("esp_wifi_init() returned ");
  Serial.println(result);
  if (result != ESP_OK) {
    Serial.println("Failed to initialize wifi");
  }

  result = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  Serial.print("esp_wifi_get_mac() returned ");
  Serial.println(result);
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
      _deviceId = deviceMapping[i].deviceId;
      Serial.print("Device ID = ");
      Serial.println(_deviceId);
      break;
    }
  }

  if (i == deviceMappingCount) {
    Serial.println("There is no matching device!!! Using device ID 0");
  }

  if (!_loRa->begin(912900000)) {
    Serial.println("Starting LoRa failed! Waiting 60 seconds for restart...");
    delay(60000);
    Serial.println("Restarting!");
    ESP.restart();
    // while (1);
  }

  _loRa->setSpreadingFactor(10);
  _loRa->setSignalBandwidth(125000);
  _loRa->setCodingRate4(5);
  _loRa->setPreambleLength(8);
  _loRa->setSyncWord(0x12);
  _loRa->enableCrc();

  Serial.println("LoRa started successfully");

  loRaCrypto = new LoRaCrypto(&encryptionCredentials);

  // _loRa->idle();
#if defined(ENABLE_SYNC_RECEIVER)
  _loRa->receive();
#endif
}

void LoRaSync::sendBootSync() {
  Serial.print(F("Broadcasting boot-sync message for device ID = "));
  Serial.println(_deviceId);
  _sendPacket(2, (byte*) &_deviceId, sizeof(_deviceId));  // Boot-sync message
}

void LoRaSync::loop() {
#if defined(ENABLE_SYNC_SENDER)
  if (_data->forceLoRaTimeUpdate) {
    _sendNetworkTime();
    _data->forceLoRaTimeUpdate = false;
  }
  _sendCgmData(false);
  _sendPropaneLevel(false);
  _sendTemperatures(false);
#endif
#if defined(ENABLE_SYNC_RECEIVER)
  // Serial.println("ENABLE_SYNC_RECEIVER");
  _receiveLoRaData();
#endif
}

#if defined(ENABLE_SYNC)  // Receivers will send boot-sync messages
void LoRaSync::_sendPacket(uint16_t messageType, byte* data, uint dataLength) {
  // _loRa->idle();
  _loRa->beginPacket();

  byte encryptedMessage[255];
  uint encryptedMessageLength;
  uint32_t counter = loRaCrypto->encrypt(encryptedMessage, &encryptedMessageLength, _deviceId, messageType, data, dataLength);
  _loRa->write(encryptedMessage, encryptedMessageLength);

  Serial.print("Sending packet: device ID = ");
  Serial.print(_deviceId);
  Serial.print(", counter = ");
  Serial.print(counter);
  Serial.print(", type = ");
  Serial.print(messageType);
  Serial.print(", length = ");
  Serial.println(dataLength);

  _loRa->endPacket();

  // delay(50);  // Wait for the message to transmit

  // _loRa->receive();

  // _loRa->sleep();
}

void LoRaSync::_sendNetworkTime() {
#if defined(DATA_COLLECTOR)
  struct clockInfo_struct clockInfo;

  Serial.println("LoRa: sending network time");
  clockInfo.time = time(nullptr);
  clockInfo.dstBegin = _data->dstBegin;
  clockInfo.dstEnd = _data->dstEnd;
  clockInfo.standardTimezoneOffset = _data->standardTimezoneOffset;
  clockInfo.daylightTimezoneOffset = _data->daylightTimezoneOffset;

  _sendPacket(1, (byte*) &clockInfo, sizeof(clockInfo));  // Time update
#endif
}

void LoRaSync::_sendCgmData(bool forceUpdate) {
  if ((_data->mgPerDl != _oldData->mgPerDl) ||
      _cgmGuaranteeTimer.isExpired(600000) ||  // Once every ten minutes
      forceUpdate) {
    struct cgm_struct cgm = { _data->mgPerDl & 0xFFFF, time(nullptr) };
    _sendPacket(29, (byte*) &cgm, sizeof(cgm));  // CGM reading
    _cgmGuaranteeTimer.reset();
    _oldData->mgPerDl = _data->mgPerDl;
  }
}

void LoRaSync::_sendPropaneLevel(bool forceUpdate) {
  if ((_data->propaneLevel != _oldData->propaneLevel) ||
      _propaneGuaranteeTimer.isExpired(3600000) ||  // Once per hour
      forceUpdate) {
    byte data = (_data->propaneLevel >= 0 ? _data->propaneLevel & 0xFF : 0xFF);
    _sendPacket(30, (byte*) &data, sizeof(data));  // Propane level in percent
    _propaneGuaranteeTimer.reset();
    _oldData->propaneLevel = _data->propaneLevel;
  }
}

void LoRaSync::_sendTemperatures(bool forceUpdate) {
  if ((_data->indoorTemperature != _oldData->indoorTemperature) ||
      (_data->indoorHumidity != _oldData->indoorHumidity) ||
      (_data->outdoorTemperature != _oldData->outdoorTemperature) ||
      (_data->outdoorHumidity != _oldData->outdoorHumidity) ||
      _temperatureGuaranteeTimer.isExpired(300000) ||  // Once every five minutes
      forceUpdate) {
    struct temperature_struct temperatures;

    temperatures.indoorTemperature = _data->indoorTemperature;
    temperatures.indoorHumidity = _data->indoorHumidity;
    temperatures.outdoorTemperature = _data->outdoorTemperature;
    temperatures.outdoorHumidity = _data->outdoorHumidity;

    _sendPacket(31, (byte*) &temperatures, sizeof(temperatures) - sizeof(temperatures.padding0));
    _temperatureGuaranteeTimer.reset();
    _oldData->indoorHumidity = _data->indoorHumidity;
    _oldData->indoorTemperature = _data->indoorTemperature;
    _oldData->outdoorHumidity = _data->outdoorHumidity;
    _oldData->outdoorTemperature = _data->outdoorTemperature;
  }
}
#endif

#if defined(ENABLE_SYNC_RECEIVER)
void LoRaSync::_receiveLoRaData() {
  // try to parse encrypted message
  int encryptedMessageSize = _loRa->parsePacket();
  // Serial.println(encryptedMessageSize);
  if (!encryptedMessageSize) {
    return;
  }

  // received an encrypted message
  Serial.print("Received message, size = ");
  Serial.print(encryptedMessageSize);
  // print RSSI of message
  Serial.print(" with RSSI ");
  Serial.print(_loRa->packetRssi());

  // read encrypted message
  byte encryptedMessage[255];
  uint encryptedMessageLength = 0;
  while (_loRa->available()) {
    encryptedMessage[encryptedMessageLength++] = _loRa->read();
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
#if !defined(DATA_COLLECTOR)
      struct clockInfo_struct clockInfo;
      memcpy(&clockInfo, messageData, sizeof(clockInfo));

      struct timeval tv;
      tv.tv_sec = clockInfo.time;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      _data->dstBegin = clockInfo.dstBegin;
      _data->dstEnd = clockInfo.dstEnd;
      _data->standardTimezoneOffset = clockInfo.standardTimezoneOffset;
      _data->daylightTimezoneOffset = clockInfo.daylightTimezoneOffset;
      _data->forceDisplayTimeUpdate = true;
      // We won't change the value of _data->forceDisplayTimeUpdate for the following reasons:
      //   1. We don't want to keep bouncing updates back and forth between devices when they receive a time from another device,
      //      so we don't want to set this value to true, and
      //   2. If _data->forceDisplayTimeUpdate was already set to true then that means that an update came from someplace else
      //      and we want that update to finish
      // _data->forceDisplayTimeUpdate = true;
#endif

      break;

    // Boot-sync
    case 2:
#if defined(ENABLE_SYNC_SENDER)
      {
        sprintf(displayBuffer, "\"boot-sync messageId %d with deviceId = %d at time %" PRId64 "\"", messageMetadata.counter, *((uint16_t*) messageData), time(nullptr));
        Serial.println(displayBuffer);

        _sendNetworkTime();
        _sendCgmData(true);
        _sendPropaneLevel(true);
        _sendTemperatures(true);
      }
#endif
      break;

    case 29:
      {
        struct cgm_struct cgm;

        memcpy(&cgm, messageData, sizeof(cgm));

        _data->mgPerDl = scrubMgPerDl(cgm.mgPerDl);
        sprintf(displayBuffer, "\"messageId %d with cgm reading = %d at time %" PRId64 "\"", messageMetadata.counter, _data->mgPerDl, cgm.time);
        Serial.println(displayBuffer);
      }
      break;

    case 30:
      {
        _data->propaneLevel = scrubPropaneLevel((byte) messageData[0]);
        sprintf(displayBuffer, "\"messageId %d with propane reading = %d at time %" PRId64 "\"", messageMetadata.counter, _data->propaneLevel, time(nullptr));
        Serial.println(displayBuffer);
      }
      break;

    case 31:
      {
        struct temperature_struct temperatures;

        if (messageMetadata.length < (sizeof(temperatures) - sizeof(temperatures.padding0))) {
          Serial.print("error: the message has the wrong length. It is ");
          Serial.print(messageMetadata.length);
          Serial.println(" byte(s) long, but must be at least 10 bytes");
          break;
        }

        memcpy(&temperatures, messageData, sizeof(temperatures));
        _data->indoorTemperature = scrubTemperature(temperatures.indoorTemperature);
        _data->indoorHumidity = scrubHumidity(temperatures.indoorHumidity);
        _data->outdoorTemperature = scrubTemperature(temperatures.outdoorTemperature);
        _data->outdoorHumidity = scrubHumidity(temperatures.outdoorHumidity);
        sprintf(displayBuffer,
                "\"messageId %d with temperature readings (IT) = %f, (IH) = %d, (OT) = %f, (OH) = %d at time %" PRId64 "\"",
                messageMetadata.counter,
                _data->indoorTemperature,
                _data->indoorHumidity,
                _data->outdoorTemperature,
                _data->outdoorHumidity,
                time(nullptr));
        Serial.println(displayBuffer);
      }
      break;

    default:
      sprintf(displayBuffer, "unknown message type %d", messageMetadata.type);
      Serial.println(displayBuffer);
  }
}
#endif
