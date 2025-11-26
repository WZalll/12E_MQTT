#include "LedSubsystem.h"

namespace DeviceCore {

namespace {
constexpr unsigned long kProvisioningBlinkIntervalMs = 500UL;
}

LedSubsystem::LedSubsystem(uint8_t userPin,
                           uint8_t errPin,
                           unsigned long userPulseDuration,
                           unsigned long errPulseDuration)
    : _userPin(userPin),
      _errPin(errPin),
      _userPulseDuration(userPulseDuration ? userPulseDuration : 150UL),
      _errPulseDuration(errPulseDuration ? errPulseDuration : 150UL),
      _userPulseActive(false),
      _errPulseActive(false),
      _userPulseStartMs(0),
      _errPulseStartMs(0),
      _errBlinking(false),
      _errBlinkState(false),
      _errBlinkStartMs(0) {}

void LedSubsystem::begin() {
  pinMode(_userPin, OUTPUT);
  pinMode(_errPin, OUTPUT);
  digitalWrite(_userPin, HIGH);
  digitalWrite(_errPin, LOW);
  _errBlinking = false;
  _errBlinkState = false;
  _errBlinkStartMs = 0;
}

void LedSubsystem::requestUserPulse(unsigned long now) {
  digitalWrite(_userPin, LOW);
  _userPulseActive = true;
  _userPulseStartMs = now;
}

void LedSubsystem::requestErrPulse(unsigned long now) {
  digitalWrite(_errPin, LOW);
  _errPulseActive = true;
  _errPulseStartMs = now;
}

void LedSubsystem::loop(unsigned long now, bool networkReady) {
  if (_userPulseActive && now - _userPulseStartMs >= _userPulseDuration) {
    digitalWrite(_userPin, HIGH);
    _userPulseActive = false;
  }

  if (_errPulseActive) {
    if (now - _errPulseStartMs >= _errPulseDuration) {
      _errPulseActive = false;
    } else {
      return;
    }
  }

  if (_errBlinking) {
    if (now - _errBlinkStartMs >= kProvisioningBlinkIntervalMs) {
      _errBlinkStartMs = now;
      _errBlinkState = !_errBlinkState;
      digitalWrite(_errPin, _errBlinkState ? LOW : HIGH);
    }
  } else {
    digitalWrite(_errPin, networkReady ? HIGH : LOW);
  }
}

void LedSubsystem::setPulseDurations(unsigned long userPulseDuration, unsigned long errPulseDuration) {
  _userPulseDuration = userPulseDuration ? userPulseDuration : 150UL;
  _errPulseDuration = errPulseDuration ? errPulseDuration : 150UL;
}

void LedSubsystem::setErrBlinking(bool blinking) {
  if (_errBlinking == blinking) {
    return;
  }
  _errBlinking = blinking;
  _errBlinkStartMs = millis();
  _errBlinkState = false;
  digitalWrite(_errPin, blinking ? LOW : HIGH);
}

}  // namespace DeviceCore
