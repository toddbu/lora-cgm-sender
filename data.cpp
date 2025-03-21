#include "lora-cgm-sender.ino.globals.h"
#include "data.h"

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
