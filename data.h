#pragma once

struct data_struct {
  time_t time;
  time_t dstBegin;
  time_t dstEnd;
  int32_t standardTimezoneOffset;
  int32_t daylightTimezoneOffset;
  bool forceDisplayTimeUpdate;
  bool forceLoRaTimeUpdate;
  ushort mgPerDl;
  byte propaneLevel;
  float indoorTemperature;
  byte indoorHumidity;
  float outdoorTemperature;
  byte outdoorHumidity;
};
