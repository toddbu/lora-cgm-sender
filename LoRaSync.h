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
    ExpirationTimer _cgmGuaranteeTimer;
    ExpirationTimer _propaneGuaranteeTimer;
    ExpirationTimer _temperatureGuaranteeTimer;

    int _processPacketState;
    ExpirationTimer _processPacketTimer;

    void _sendPacket(uint16_t messageType, byte* data, uint dataLength, bool randomizeTiming = false);
    void _processQueuedPackets();
    void _sendNetworkTime(bool randomizeTiming);
    void _sendCgmData(bool forceUpdate);
    void _sendPropaneLevel(bool forceUpdate);
    void _sendTemperatures(bool forceUpdate);
    void _receiveLoRaData();

  public:
    LoRaSync(volatile struct data_struct* data, SPIClass* spi);
    ~LoRaSync();

    void setup();
    void loop();

    void sendBootSync();
    uint16_t deviceId() { return _deviceId; };
};
