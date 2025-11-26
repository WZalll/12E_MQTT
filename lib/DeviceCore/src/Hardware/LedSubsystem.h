#pragma once

#include <Arduino.h>

namespace DeviceCore {

class LedSubsystem {
public:
  LedSubsystem(uint8_t userPin,
               uint8_t errPin,
               unsigned long userPulseDuration,
               unsigned long errPulseDuration);

  void begin();
  void requestUserPulse(unsigned long now);
  void requestErrPulse(unsigned long now);
  void loop(unsigned long now, bool networkReady);
  void setPulseDurations(unsigned long userPulseDuration, unsigned long errPulseDuration);
  void setErrBlinking(bool blinking);

private:
  uint8_t _userPin;
  uint8_t _errPin;
  unsigned long _userPulseDuration;
  unsigned long _errPulseDuration;
  bool _userPulseActive;
  bool _errPulseActive;
  unsigned long _userPulseStartMs;
  unsigned long _errPulseStartMs;
  bool _errBlinking;
  bool _errBlinkState;
  unsigned long _errBlinkStartMs;
};

}  // namespace DeviceCore
