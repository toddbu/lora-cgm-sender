#pragma once

struct data_struct {
  time_t time;
  time_t dstBegin;
  time_t dstEnd;
  int32_t standardTimezoneOffset;
  int32_t daylightTimezoneOffset;
  bool forceDisplayTimeUpdate;
  bool forceLoRaTimeUpdate;
  long mgPerDl;
  int propaneLevel;
  double temperature;
};
