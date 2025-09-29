#include "DeviceCore.h"

#include <ArduinoJson.h>
#include <cstring>

namespace DeviceCore {

namespace {
constexpr size_t kDefaultSerialBufferLimit = 256;
constexpr unsigned long kDefaultSerialBaud = 115200UL;
constexpr unsigned long kWifiRetryIntervalMs = 2000UL;
constexpr unsigned long kMqttRetryIntervalMs = 2000UL;
}  // namespace

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
      _errPulseStartMs(0) {}

void LedSubsystem::begin() {
  pinMode(_userPin, OUTPUT);
  pinMode(_errPin, OUTPUT);
  digitalWrite(_userPin, HIGH);
  digitalWrite(_errPin, LOW);
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

  digitalWrite(_errPin, networkReady ? HIGH : LOW);
}

void LedSubsystem::setPulseDurations(unsigned long userPulseDuration, unsigned long errPulseDuration) {
  _userPulseDuration = userPulseDuration ? userPulseDuration : 150UL;
  _errPulseDuration = errPulseDuration ? errPulseDuration : 150UL;
}

SerialForwarder::SerialForwarder(size_t bufferLimit)
    : _buffer(),
      _escapePending(false),
      _bufferLimit(bufferLimit ? bufferLimit : kDefaultSerialBufferLimit) {
  _buffer.reserve(_bufferLimit);
}

void SerialForwarder::resetBuffer(size_t newLimit) {
  _bufferLimit = newLimit ? newLimit : kDefaultSerialBufferLimit;
  _buffer = "";
  _escapePending = false;
  _buffer.reserve(_bufferLimit);
}

void SerialForwarder::process(unsigned long now,
                              const DeviceConfig& config,
                              bool wifiConnected,
                              bool mqttConnected,
                              PubSubClient& client,
                              LedSubsystem& leds) {
  while (Serial.available() > 0) {
    char ch = static_cast<char>(Serial.read());

    if (_escapePending) {
      if (ch == 'n' || ch == 'N' || ch == 'r' || ch == 'R') {
        flushBuffer(now, config, wifiConnected, mqttConnected, client, leds);
      } else {
        if (_buffer.length() < _bufferLimit) {
          _buffer += '\\';
        }
        if (_buffer.length() < _bufferLimit) {
          _buffer += ch;
        }
      }
      _escapePending = false;
      continue;
    }

    if (ch == '\\') {
      _escapePending = true;
      continue;
    }

    if (ch == '\r' || ch == '\n') {
      flushBuffer(now, config, wifiConnected, mqttConnected, client, leds);
      continue;
    }

    if (_buffer.length() < _bufferLimit) {
      _buffer += ch;
    }
  }
}

void SerialForwarder::flushBuffer(unsigned long now,
                                  const DeviceConfig& config,
                                  bool wifiConnected,
                                  bool mqttConnected,
                                  PubSubClient& client,
                                  LedSubsystem& leds) {
  if (_buffer.length() == 0) {
    return;
  }

  if (!wifiConnected) {
    Serial.println("Serial forward skipped: WiFi not connected.");
    leds.requestErrPulse(now);
  } else if (!mqttConnected) {
    Serial.println("Serial forward skipped: MQTT not connected.");
    leds.requestErrPulse(now);
  } else {
    bool serialOk = publishMessage(client, config.serialTopic, _buffer);
    bool sameTopic = (config.serialTopic && config.primaryTopic && std::strcmp(config.serialTopic, config.primaryTopic) == 0);
    bool primaryOk = sameTopic ? serialOk : publishMessage(client, config.primaryTopic, _buffer);

    if (serialOk || primaryOk) {
      if (!serialOk && primaryOk) {
        Serial.println("Serial topic publish failed, mirrored via primary topic.");
      }
      Serial.print("Forwarded serial: ");
      Serial.println(_buffer);
      leds.requestUserPulse(now);
    } else {
      Serial.println("Serial forward failed: MQTT publish error.");
      leds.requestErrPulse(now);
    }
  }

  _buffer = "";
  _escapePending = false;
}

bool SerialForwarder::publishMessage(PubSubClient& client, const char* topic, const String& payload) {
  if (!topic || topic[0] == '\0') {
    return false;
  }
  return client.publish(topic, payload.c_str());
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

DeviceController* DeviceController::s_instance = nullptr;

DeviceController::DeviceController(const DeviceConfig& config)
    : _config(config),
      _mqttClient(_wifiClient),
      _leds(config.pinUser1, config.pinErr, config.user1PulseDuration, config.errPulseDuration),
      _serialForwarder(config.serialBufferLimit),
      _mqttLayer(_mqttClient, _config),
      _heartbeatEnabled(true),
      _lastWifiRetryMs(0) {
  if (_config.serialBufferLimit == 0) {
    _config.serialBufferLimit = kDefaultSerialBufferLimit;
  }
  if (_config.serialBaud == 0) {
    _config.serialBaud = kDefaultSerialBaud;
  }
  if (_config.errPulseDuration == 0) {
    _config.errPulseDuration = 150UL;
  }
  if (_config.user1PulseDuration == 0) {
    _config.user1PulseDuration = 150UL;
  }
  if (_config.heartbeatInterval == 0) {
    _config.heartbeatInterval = 5000UL;
  }

  _serialForwarder.resetBuffer(_config.serialBufferLimit);
  _leds.setPulseDurations(_config.user1PulseDuration, _config.errPulseDuration);
}

void DeviceController::begin() {
  s_instance = this;
  Serial.begin(_config.serialBaud);

  _leds.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(_config.ssid, _config.password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi RSSI: ");
  Serial.println(WiFi.RSSI());

  _mqttLayer.begin(DeviceController::mqttCallback);

  while (!_mqttLayer.ensureConnected(millis())) {
    delay(kMqttRetryIntervalMs);
  }
  _lastWifiRetryMs = millis();
  _leds.loop(millis(), WiFi.status() == WL_CONNECTED && _mqttLayer.isConnected());
}

void DeviceController::loop() {
  unsigned long now = millis();

  ensureWifiConnected(now);

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  bool mqttConnected = false;

  if (wifiConnected) {
    mqttConnected = _mqttLayer.ensureConnected(now);
    if (mqttConnected) {
      _mqttLayer.loop();
      if (_mqttLayer.handleHeartbeat(now, _heartbeatEnabled)) {
        _leds.requestUserPulse(now);
      }
    }
  }

  _serialForwarder.process(now, _config, wifiConnected, mqttConnected, _mqttClient, _leds);
  _leds.loop(now, wifiConnected && mqttConnected);
  delay(10);
}

void DeviceController::setHeartbeatEnabled(bool enabled) {
  _heartbeatEnabled = enabled;
}

void DeviceController::ensureWifiConnected(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (now - _lastWifiRetryMs < kWifiRetryIntervalMs) {
    return;
  }
  Serial.println("WiFi disconnected, retrying...");
  WiFi.disconnect();
  WiFi.begin(_config.ssid, _config.password);
  _lastWifiRetryMs = now;
}

void DeviceController::mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!s_instance) {
    return;
  }
  s_instance->onMqttMessage(topic, payload, length);
}

void DeviceController::onMqttMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");

  String msg;
  for (unsigned int i = 0; i < length; i++) {
    Serial.print(static_cast<char>(payload[i]));
    msg += static_cast<char>(payload[i]);
  }
  Serial.println();

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, msg) == DeserializationError::Ok) {
    if (doc["cmd"] == "heartbeat") {
      bool enable = doc["enable"];
      setHeartbeatEnabled(enable);
      Serial.print("[MQTT] Heartbeat switched to: ");
      Serial.println(enable ? "ON" : "OFF");
    }
  }
}

}  // namespace DeviceCore
