#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

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
  unsigned long heartbeatInterval;
  unsigned long user1PulseDuration;
  unsigned long errPulseDuration;
  size_t serialBufferLimit;
  unsigned long serialBaud;
};

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

private:
  uint8_t _userPin;
  uint8_t _errPin;
  unsigned long _userPulseDuration;
  unsigned long _errPulseDuration;
  bool _userPulseActive;
  bool _errPulseActive;
  unsigned long _userPulseStartMs;
  unsigned long _errPulseStartMs;
};

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

class DeviceController {
public:
  explicit DeviceController(const DeviceConfig& config);

  void begin();
  void loop();

  void setHeartbeatEnabled(bool enabled);
  bool heartbeatEnabled() const { return _heartbeatEnabled; }

private:
  static DeviceController* s_instance;

  DeviceConfig _config;
  WiFiClient _wifiClient;
  PubSubClient _mqttClient;

  LedSubsystem _leds;
  SerialForwarder _serialForwarder;
  MqttLayer _mqttLayer;
  bool _heartbeatEnabled;
  unsigned long _lastWifiRetryMs;

  static void mqttCallback(char* topic, byte* payload, unsigned int length);

  void onMqttMessage(char* topic, byte* payload, unsigned int length);

  void ensureWifiConnected(unsigned long now);
};

}  // namespace DeviceCore
