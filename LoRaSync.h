#pragma once

#include <SPI.h>
#include <LoRa.h>
#include <Arduino.h>
#include "credentials.h"
#include "data.h"
#include <ExpirationTimer.h>
#include <LoRaCrypto.h>
#include <LoRaCryptoCreds.h>

class LoRaSync {
  private:
    volatile struct data_struct* _data;
    struct data_struct* _oldData;
    uint16_t _deviceId = 0;
    SPIClass* _spi;
    LoRaClass* _loRa;
    time_t _dstBegins = 0;
    time_t _dstEnds = 0;
    ExpirationTimer _cgmGuaranteeTimer;
    ExpirationTimer _propaneGuaranteeTimer;

    void _sendPacket(uint16_t messageType, byte* data, uint dataLength);
    void _sendNetworkTime();
    void _sendCgmData(bool forceUpdate);
    void _sendPropaneLevel(bool forceUpdate);
    void _receiveLoRaData();

  public:
    LoRaSync(volatile struct data_struct* data, SPIClass* spi);
    ~LoRaSync();

    void setup();
    void loop();

    uint16_t deviceId() { return _deviceId; };
};
