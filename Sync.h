#pragma once

#include <SPI.h>
#include <LoRa.h>
#include <Arduino.h>
#include "credentials.h"
#include "data.h"
#include <ExpirationTimer.h>
#include <LoRaCrypto.h>
#include <LoRaCryptoCreds.h>

class Sync {
  private:
    volatile struct data_struct* _data;
    struct data_struct* _oldData;
    uint16_t _deviceId = 0;
    SPIClass* _spi;
    LoRaClass* _loRa;
    ExpirationTimer _cgmGuaranteeTimer;
    ExpirationTimer _propaneGuaranteeTimer;

    void _sendPacket(uint16_t messageType, byte* data, uint dataLength);
    void _sendNetworkTime();
    void _sendCgmData(bool forceUpdate);
    void _sendPropaneLevel(bool forceUpdate);
    void _receiveLoRaData();

  public:
    Sync(volatile struct data_struct* data, SPIClass* spi);
    ~Sync();

    void setup();
    void loop();

    uint16_t deviceId() { return _deviceId; };
};
