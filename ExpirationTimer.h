#pragma once

class ExpirationTimer {
  private:
    unsigned long _lastResetTime;
    bool _forceExpired;

  public:
    ExpirationTimer(unsigned long lastResetTime = millis()) {
      reset();
    };
    bool isExpired(unsigned long delay) {
      return _forceExpired ||
             ((_lastResetTime != ULONG_MAX) && (((millis() - _lastResetTime) > delay)));
    };
    void reset(unsigned long lastResetTime = millis()) {
      _lastResetTime = lastResetTime;
      _forceExpired = false;
    };
    void forceExpired() {
      _forceExpired = true;
    };
};
