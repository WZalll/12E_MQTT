#include "MqttLayer.h"
#include <cstring>

namespace DeviceCore {

namespace {
constexpr unsigned long kMqttRetryIntervalMs = 2000UL;
}

MqttLayer::MqttLayer(PubSubClient& client, const DeviceConfig& config)
    : _client(client),
      _config(config),
      _lastHeartbeatMs(0),
      _lastRetryMs(0) {}

void MqttLayer::begin(MQTT_CALLBACK_SIGNATURE) {
  _client.setServer(_config.mqttServer, _config.mqttPort);
  _client.setCallback(callback);
}

bool MqttLayer::ensureConnected(unsigned long now) {
  if (_client.connected()) {
    return true;
  }

  if (now - _lastRetryMs < kMqttRetryIntervalMs) {
    return false;
  }

  Serial.println("MQTT disconnected, retrying...");
  _lastRetryMs = now;
  if (tryConnect()) {
    _lastHeartbeatMs = now;
    return true;
  }
  return false;
}

void MqttLayer::loop() {
  _client.loop();
}

bool MqttLayer::handleHeartbeat(unsigned long now, bool heartbeatEnabled) {
  if (!heartbeatEnabled || !_client.connected()) {
    return false;
  }
  if (!_config.primaryTopic || _config.primaryTopic[0] == '\0') {
    return false;
  }
  if (now - _lastHeartbeatMs < _config.heartbeatInterval) {
    return false;
  }

  String msg = "ESP heartbeat: ";
  msg += now / 1000UL;
  if (_client.publish(_config.primaryTopic, msg.c_str())) {
    Serial.print("Published: ");
    Serial.println(msg);
    _lastHeartbeatMs = now;
    return true;
  }
  Serial.println("Heartbeat publish failed.");
  return false;
}

bool MqttLayer::publish(const char* topic, const String& payload) {
  if (!topic || topic[0] == '\0') {
    return false;
  }
  return _client.publish(topic, payload.c_str());
}

bool MqttLayer::isConnected() const {
  return _client.connected();
}

bool MqttLayer::tryConnect() {
  Serial.print("Attempting MQTT connection...");
  const char* clientId = (_config.clientId && _config.clientId[0] != '\0') ? _config.clientId : "esp_client";
  if (_client.connect(clientId)) {
    Serial.println("connected");
    if (_config.primaryTopic && _config.primaryTopic[0] != '\0') {
      _client.subscribe(_config.primaryTopic);
      Serial.print("Subscribed to topic: ");
      Serial.println(_config.primaryTopic);
      _client.publish(_config.primaryTopic, "Hello from ESP32!");
      Serial.println("MQTT: Published 'Hello from ESP32!'");
    }
    if (_config.serialTopic && _config.serialTopic[0] != '\0' && (!_config.primaryTopic || std::strcmp(_config.serialTopic, _config.primaryTopic) != 0)) {
      _client.subscribe(_config.serialTopic);
      Serial.print("Subscribed to serial topic: ");
      Serial.println(_config.serialTopic);
    }
    return true;
  }
  Serial.print("failed, rc=");
  Serial.println(_client.state());
  return false;
}

}  // namespace DeviceCore
