#include "lora-cgm-sender.ino.globals.h"
#include "data.h"

ushort scrubMgPerDl(ushort mgPerDl) {
  if ((mgPerDl < 0) ||
      (mgPerDl > 400)) {
    return UNKNOWN_MG_PER_DL;
  }

  return mgPerDl;
}

byte scrubPropaneLevel(byte propaneLevel) {
  if ((propaneLevel < 0) ||
      (propaneLevel > 100)) {
    return UNKNOWN_MG_PER_DL;
  }

  return propaneLevel;
}

float scrubTemperature(float temperature) {
  if (temperature <= UNKNOWN_TEMPERATURE) {
    return UNKNOWN_TEMPERATURE;
  }

  if (temperature > 199.0) {
    return 199.0;
  }
  
  return temperature;
}

extern byte scrubHumidity(byte humidity) {
  if ((humidity < 0) ||
      (humidity > 100)) {
    return UNKNOWN_HUMIDITY;
  }

  return humidity;
}
