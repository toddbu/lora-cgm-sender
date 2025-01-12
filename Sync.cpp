#include <esp_wifi.h>
#include <Crypto.h>
#include <ChaCha.h>
#include "Sync.h"

struct deviceMapping_struct {
  const char* macAddress;
  uint16_t deviceId;
};

struct deviceMapping_struct deviceMapping[] = {
  {"24:58:7c:dc:99:d0", 32},  // Large display #1
  {"00:00:00:00:00:00", 33},  // Small display
  {"34:b7:da:59:0a:90", 34}  // Large display #2
};

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

Sync::Sync(volatile struct data_struct* data, SPIClass* spi) {
  _data = data;
  _oldData = (struct data_struct*) malloc(sizeof(struct data_struct));
  memcpy(_oldData, (const void*) _data, sizeof(struct data_struct));

  _spi = spi;
  _loRa = new LoRaClass();
}

Sync::~Sync() {
  delete _loRa;

  free(_oldData);
}

void Sync::setup() {
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
  // _loRa->receive();
#endif
}

void Sync::loop() {
#if defined(ENABLE_SYNC_SENDER)
  _sendCgmData(data.mgPerDl, false);
  _sendPropaneLevel(data.propaneLevel, false);
#endif
#if defined(ENABLE_SYNC_RECEIVER)
  // Serial.println("ENABLE_SYNC_RECEIVER");
  _receiveLoRaData();
#endif
}

#if defined(ENABLE_SYNC_SENDER)
void Sync::_sendPacket(uint16_t messageType, byte* data, uint dataLength) {
  // _loRa->idle();
  _loRa->beginPacket();

  byte encryptedMessage[255];
  uint encryptedMessageLength;
  uint32_t counter = loRaCrypto->encrypt(encryptedMessage, &encryptedMessageLength, deviceId, messageType, data, dataLength);
  _loRa->write(encryptedMessage, encryptedMessageLength);

  Serial.print("Sending packet: ");
  Serial.print(counter);
  Serial.print(", type = ");
  Serial.print(messageType);
  Serial.print(", length = ");
  Serial.println(encryptedMessageLength);

  _spi->endPacket();

  delay(500);  // Wait for the message to transmit

  _loRa->receive();

  // _loRa->sleep();
}

void Sync::_sendNetworkTime() {
  struct clockInfo_struct clockInfo;

  clockInfo.time = time(nullptr);
  clockInfo.dstBegins = dstBegins;
  clockInfo.dstEnds = dstEnds;

  sendPacket(1, (byte*) &clockInfo, sizeof(clockInfo));  // Time update
}

void Sync::_sendCgmData(long mgPerDl, bool forceUpdate) {
  if ((_data->mgPerDl != _oldData->mgPerDl) ||
      cgmGuaranteeTimer.isExpired(600000) ||  // Once every ten minutes per openweathermap.com
      forceUpdate) {
    struct cgm_struct cgm = { _data->mgPerDl & 0xFFFF, time(nullptr) };
    sendPacket(29, (byte*) &cgm, sizeof(cgm));  // CGM reading
    cgmGuaranteeTimer.reset();
    _oldData->mgPerDl = _data->mgPerDl;
  }
}

void Sync::_sendPropaneLevel(int propaneLevel, bool forceUpdate) {
  if ((_data->propaneLevel != _oldData->propaneLevel) ||
      _propaneGuaranteeTimer.isExpired(3600000) ||  // Once per hour
      forceUpdate) {
    byte data = (_data->propaneLevel >= 0 ? _data->propaneLevel & 0xFF : 0xFF);
    sendPacket(30, (byte*) &data, sizeof(data));  // Propane level in percent
    _propaneGuaranteeTimer.reset();
    _oldData->propaneLevel = _data->propaneLevel;
  }
}
#endif

#if defined(ENABLE_SYNC_RECEIVER)
void Sync::_receiveLoRaData() {
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
#if defined(ENABLE_SYNC_SENDER)
      {
        sprintf(displayBuffer, "\"boot-sync messageId %d with deviceId = %d at time %" PRId64 "\"", messageMetadata.counter, *((uint16_t*) messageData), time(nullptr));
        Serial.println(displayBuffer);

        sendNetworkTime();
        sendCgmData(true);
        sendPropaneLevel(true);
      }
#endif
      break;

    case 29:
      {
        struct cgm_struct cgm;

        memcpy(&cgm, messageData, sizeof(cgm));

        _data->mgPerDl = cgm.mgPerDl;
        sprintf(displayBuffer, "\"messageId %d with cgm reading = %d at time %" PRId64 "\"", messageMetadata.counter, _data->mgPerDl, cgm.time);
        Serial.println(displayBuffer);
      }
      break;

    case 30:
      {
        _data->propaneLevel = (int) messageData[0];
        sprintf(displayBuffer, "\"messageId %d with propane reading = %d at time %" PRId64 "\"", messageMetadata.counter, _data->propaneLevel, time(nullptr));
        Serial.println(displayBuffer);
      }
      break;

    default:
      sprintf(displayBuffer, "unknown message type %d", messageMetadata.type);
      Serial.println(displayBuffer);
  }
}
#endif
