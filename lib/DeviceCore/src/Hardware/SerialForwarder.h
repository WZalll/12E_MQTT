#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include "../Config/DeviceConfig.h"
#include "LedSubsystem.h"

namespace DeviceCore {

class SerialForwarder {
public:
  explicit SerialForwarder(size_t bufferLimit);

  void resetBuffer(size_t newLimit);
  void process(unsigned long now,
               const DeviceConfig& config,
               bool wifiConnected,
               bool mqttConnected,
               PubSubClient& client,
               LedSubsystem& leds);

private:
  String _buffer;
  bool _escapePending;
  size_t _bufferLimit;

  void flushBuffer(unsigned long now,
                   const DeviceConfig& config,
                   bool wifiConnected,
                   bool mqttConnected,
                   PubSubClient& client,
                   LedSubsystem& leds);
  bool publishMessage(PubSubClient& client, const char* topic, const String& payload);
};

}  // namespace DeviceCore
