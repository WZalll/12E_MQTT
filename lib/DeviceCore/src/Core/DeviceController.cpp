#include "DeviceController.h"
#include <ArduinoJson.h>

namespace DeviceCore {

namespace {
constexpr unsigned long kDefaultSerialBaud = 115200UL;
constexpr size_t kDefaultSerialBufferLimit = 256;
constexpr unsigned long kWifiRetryIntervalMs = 2000UL;
constexpr unsigned long kMqttRetryIntervalMs = 2000UL;
constexpr unsigned long kResetHoldDurationMs = 10000UL;
constexpr unsigned long kWifiConnectTimeoutMs = 20000UL;
}

DeviceController* DeviceController::s_instance = nullptr;

DeviceController::DeviceController(const DeviceConfig& config)
    : _config(config),
      _mqttClient(_wifiClient),
      _leds(config.pinUser1, config.pinErr, config.user1PulseDuration, config.errPulseDuration),
      _serialForwarder(config.serialBufferLimit),
      _mqttLayer(_mqttClient, _config),
      _credentialStore(),
      _provisioningManager(_credentialStore, config.maintenancePhone, config.userManualUrl),
  _credentials(),
      _heartbeatEnabled(true),
      _lastWifiRetryMs(0),
      _lastProvisioningCheckMs(0),
      _resetPressStartMs(0),
      _resetTriggered(false),
  _wifiReady(false),
  _initialSsid(config.ssid),
  _initialPassword(config.password) {
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

  if (_config.pinReset == 0xFF) {
    _config.pinReset = 14;  // Default to GPIO14 if not set
  }

  _ssidBuffer[0] = '\0';
  _passwordBuffer[0] = '\0';

  _serialForwarder.resetBuffer(_config.serialBufferLimit);
  _leds.setPulseDurations(_config.user1PulseDuration, _config.errPulseDuration);
}

void DeviceController::begin() {
  s_instance = this;
  Serial.begin(_config.serialBaud);

  pinMode(_config.pinReset, INPUT_PULLUP);

  _leds.begin();

  initializeCredentials();

  _mqttLayer.begin(DeviceController::mqttCallback);

  if (_credentials.valid) {
    if (!connectWifi()) {
      startProvisioning();
    }
  } else {
    startProvisioning();
  }

  _lastWifiRetryMs = millis();
  _lastProvisioningCheckMs = millis();
  _leds.loop(millis(), WiFi.status() == WL_CONNECTED && _mqttLayer.isConnected());
}

void DeviceController::loop() {
  unsigned long now = millis();

  handleResetButton(now);
  handleProvisioning(now);

  bool provisioning = _provisioningManager.isProvisioning();

  if (!provisioning && _credentials.valid) {
    ensureWifiConnected(now);
  }

  bool wifiConnected = (!provisioning && _credentials.valid) ? (WiFi.status() == WL_CONNECTED) : false;
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
  if (!_credentials.valid || !_config.ssid) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    _wifiReady = true;
    return;
  }

  if (now - _lastWifiRetryMs < kWifiRetryIntervalMs) {
    return;
  }

  Serial.println("WiFi disconnected, retrying...");
  _lastWifiRetryMs = now;
  connectWifi();
}

void DeviceController::initializeCredentials() {
  if (_credentialStore.load(_credentials)) {
    Serial.println("[Credentials] Loaded from storage.");
    applyCredentials(_credentials);
    return;
  }

  if (_initialSsid && _initialSsid[0] != '\0') {
    StoredCredentials defaults;
    strncpy(defaults.ssid, _initialSsid, sizeof(defaults.ssid) - 1);
    defaults.ssid[sizeof(defaults.ssid) - 1] = '\0';
    if (_initialPassword) {
      strncpy(defaults.password, _initialPassword, sizeof(defaults.password) - 1);
      defaults.password[sizeof(defaults.password) - 1] = '\0';
    }
    defaults.valid = true;
    Serial.println("[Credentials] Using defaults from configuration.");
    applyCredentials(defaults);
    return;
  }

  clearCredentials();
}

void DeviceController::applyCredentials(const StoredCredentials& creds) {
  strncpy(_ssidBuffer, creds.ssid, sizeof(_ssidBuffer) - 1);
  _ssidBuffer[sizeof(_ssidBuffer) - 1] = '\0';
  strncpy(_passwordBuffer, creds.password, sizeof(_passwordBuffer) - 1);
  _passwordBuffer[sizeof(_passwordBuffer) - 1] = '\0';

  _config.ssid = (_ssidBuffer[0] != '\0') ? _ssidBuffer : nullptr;
  _config.password = (_passwordBuffer[0] != '\0') ? _passwordBuffer : nullptr;
  _credentials = creds;
  _credentials.valid = (_config.ssid != nullptr);
  _wifiReady = false;

  if (_credentials.valid) {
    Serial.print("[Credentials] Active SSID: ");
    Serial.println(_config.ssid);
  }
}

void DeviceController::clearCredentials() {
  memset(_ssidBuffer, 0, sizeof(_ssidBuffer));
  memset(_passwordBuffer, 0, sizeof(_passwordBuffer));
  _config.ssid = nullptr;
  _config.password = nullptr;
  _credentials = StoredCredentials();
  _credentials.valid = false;
}

bool DeviceController::connectWifi() {
  if (!_credentials.valid || !_config.ssid) {
    return false;
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(_config.ssid, _config.password);

  _lastWifiRetryMs = millis();

  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());
    Serial.print("WiFi RSSI: ");
    Serial.println(WiFi.RSSI());
    _wifiReady = true;

    // Ensure MQTT reconnect after Wi-Fi comes up
    unsigned long now = millis();
    while (!_mqttLayer.ensureConnected(now)) {
      delay(kMqttRetryIntervalMs);
      now = millis();
    }

    _leds.setErrBlinking(false);
    return true;
  }

  Serial.println("WiFi connection failed.");
  _wifiReady = false;
  return false;
}

void DeviceController::startProvisioning() {
  _leds.setErrBlinking(true);
  if (!_provisioningManager.isProvisioning()) {
    _provisioningManager.begin();
  }
}

void DeviceController::stopProvisioning() {
  if (_provisioningManager.isProvisioning()) {
    _provisioningManager.stop();
  }
  _leds.setErrBlinking(false);
}

void DeviceController::handleProvisioning(unsigned long now) {
  if (_provisioningManager.isProvisioning()) {
    _provisioningManager.loop();
  } else if (!_credentials.valid) {
    if (now - _lastProvisioningCheckMs > 1000UL) {
      startProvisioning();
    }
  }

  if (_provisioningManager.hasNewCredentials()) {
    StoredCredentials creds = _provisioningManager.consumeCredentials();
    if (creds.valid) {
      Serial.println("[Provisioning] Credentials received, attempting connection.");
      applyCredentials(creds);
      if (connectWifi()) {
        stopProvisioning();
      } else {
        Serial.println("[Provisioning] Connection failed, clearing credentials and re-entering provisioning.");
        _credentialStore.clear();
        clearCredentials();
        startProvisioning();
      }
    }
  }

  _lastProvisioningCheckMs = now;
}

void DeviceController::handleResetButton(unsigned long now) {
  if (_config.pinReset == 0xFF) {
    return;
  }

  int state = digitalRead(_config.pinReset);
  if (state == LOW) {
    if (_resetPressStartMs == 0) {
      _resetPressStartMs = now;
    }
    if (!_resetTriggered && now - _resetPressStartMs >= kResetHoldDurationMs) {
      Serial.println("[Reset] Hold detected, clearing credentials.");
      _credentialStore.clear();
      clearCredentials();
      _resetTriggered = true;
      startProvisioning();
      delay(100);
      ESP.restart();
    }
  } else {
    _resetPressStartMs = 0;
    _resetTriggered = false;
  }
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

  JsonDocument doc;
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
