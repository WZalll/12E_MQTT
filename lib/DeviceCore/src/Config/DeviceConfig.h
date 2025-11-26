#pragma once

#include <Arduino.h>

namespace DeviceCore {

struct DeviceConfig {
  const char* ssid;
  const char* password;
  const char* mqttServer;
  int mqttPort;
  const char* clientId;
  const char* primaryTopic;
  const char* serialTopic;
  uint8_t pinErr;
  uint8_t pinUser1;
  uint8_t pinReset;
  unsigned long heartbeatInterval;
  unsigned long user1PulseDuration;
  unsigned long errPulseDuration;
  size_t serialBufferLimit;
  unsigned long serialBaud;
  const char* maintenancePhone;
  const char* userManualUrl;
};

}  // namespace DeviceCore
