#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include "../Config/DeviceConfig.h"

namespace DeviceCore {

class MqttLayer {
public:
  MqttLayer(PubSubClient& client, const DeviceConfig& config);

  void begin(MQTT_CALLBACK_SIGNATURE);
  bool ensureConnected(unsigned long now);
  void loop();
  bool handleHeartbeat(unsigned long now, bool heartbeatEnabled);
  bool publish(const char* topic, const String& payload);
  bool isConnected() const;

private:
  PubSubClient& _client;
  const DeviceConfig& _config;
  unsigned long _lastHeartbeatMs;
  unsigned long _lastRetryMs;

  bool tryConnect();
};

}  // namespace DeviceCore
