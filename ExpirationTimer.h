#pragma once

class ExpirationTimer {
  private:
    unsigned long _lastResetTime;

  public:
    ExpirationTimer(unsigned long lastResetTime = millis()) {
      reset();
    };
    bool isExpired(unsigned long delay) {
      return (millis() - _lastResetTime) > delay;
    };
    void reset(unsigned long lastResetTime = millis()) {
      _lastResetTime = lastResetTime;
    }
};
