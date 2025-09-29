#include "DeviceCore.h"

#include <ArduinoJson.h>
#include <cstring>
#include <functional>

namespace DeviceCore {

namespace {
constexpr size_t kDefaultSerialBufferLimit = 256;
constexpr unsigned long kDefaultSerialBaud = 115200UL;
constexpr unsigned long kWifiRetryIntervalMs = 2000UL;
constexpr unsigned long kMqttRetryIntervalMs = 2000UL;
constexpr size_t kMaxStoredSsidLength = 32;
constexpr size_t kMaxStoredPasswordLength = 64;
constexpr size_t kCredentialEepromSize = 1 + kMaxStoredSsidLength + kMaxStoredPasswordLength;
constexpr uint8_t kCredentialMagic = 0xA5;
constexpr unsigned long kResetHoldDurationMs = 10000UL;
constexpr unsigned long kProvisioningBlinkIntervalMs = 500UL;
constexpr unsigned long kWifiConnectTimeoutMs = 20000UL;
constexpr const char* kProvisioningApSsid = "esp-sta";
}  // namespace

StoredCredentials::StoredCredentials() : valid(false) {
  ssid[0] = '\0';
  password[0] = '\0';
}

CredentialStore::CredentialStore() : _initialized(false) {}

bool CredentialStore::begin() {
  if (_initialized) {
    return true;
  }
  EEPROM.begin(kCredentialEepromSize);
  _initialized = true;
  return true;
}

bool CredentialStore::load(StoredCredentials& out) {
  if (!begin()) {
    out.valid = false;
    return false;
  }

  uint8_t magic = EEPROM.read(0);
  if (magic != kCredentialMagic) {
    out.valid = false;
    return false;
  }

  for (size_t i = 0; i < kMaxStoredSsidLength; ++i) {
    out.ssid[i] = static_cast<char>(EEPROM.read(1 + i));
  }
  out.ssid[kMaxStoredSsidLength] = '\0';

  for (size_t i = 0; i < kMaxStoredPasswordLength; ++i) {
    out.password[i] = static_cast<char>(EEPROM.read(1 + kMaxStoredSsidLength + i));
  }
  out.password[kMaxStoredPasswordLength] = '\0';

  out.valid = (out.ssid[0] != '\0');
  return out.valid;
}

bool CredentialStore::save(const StoredCredentials& creds) {
  if (!begin()) {
    return false;
  }

  EEPROM.write(0, kCredentialMagic);

  size_t ssidLen = strlen(creds.ssid);
  if (ssidLen > kMaxStoredSsidLength) {
    ssidLen = kMaxStoredSsidLength;
  }

  for (size_t i = 0; i < kMaxStoredSsidLength; ++i) {
    char ch = (i < ssidLen) ? creds.ssid[i] : '\0';
    EEPROM.write(1 + i, ch);
  }

  size_t pwdLen = strlen(creds.password);
  if (pwdLen > kMaxStoredPasswordLength) {
    pwdLen = kMaxStoredPasswordLength;
  }

  for (size_t i = 0; i < kMaxStoredPasswordLength; ++i) {
    char ch = (i < pwdLen) ? creds.password[i] : '\0';
    EEPROM.write(1 + kMaxStoredSsidLength + i, ch);
  }

  EEPROM.commit();
  return true;
}

void CredentialStore::clear() {
  if (!begin()) {
    return;
  }
  EEPROM.write(0, 0x00);
  for (size_t i = 0; i < kMaxStoredSsidLength + kMaxStoredPasswordLength; ++i) {
    EEPROM.write(1 + i, 0x00);
  }
  EEPROM.commit();
}

ProvisioningManager::ProvisioningManager(CredentialStore& store)
    : _store(store),
      _server(80),
      _provisioning(false),
      _hasPending(false),
      _pending() {}

void ProvisioningManager::begin() {
  if (_provisioning) {
    return;
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kProvisioningApSsid);

  setupRoutes();
  _server.begin();

  _provisioning = true;
  _hasPending = false;
  _pending = StoredCredentials();
  Serial.println("[Provisioning] AP started: esp-sta");
  Serial.println("[Provisioning] Connect and visit http://192.168.4.1");
}

void ProvisioningManager::stop() {
  if (!_provisioning) {
    return;
  }
  _server.stop();
  WiFi.softAPdisconnect(true);
  _provisioning = false;
}

void ProvisioningManager::loop() {
  if (_provisioning) {
    _server.handleClient();
  }
}

bool ProvisioningManager::isProvisioning() const {
  return _provisioning;
}

bool ProvisioningManager::hasNewCredentials() const {
  return _hasPending;
}

StoredCredentials ProvisioningManager::consumeCredentials() {
  _hasPending = false;
  return _pending;
}

void ProvisioningManager::setupRoutes() {
  _server.on("/", HTTP_GET, std::bind(&ProvisioningManager::handleRoot, this));
  _server.on("/submit", HTTP_POST, std::bind(&ProvisioningManager::handleSubmit, this));
  _server.onNotFound(std::bind(&ProvisioningManager::handleRoot, this));
}

void ProvisioningManager::handleRoot() {
  String page =
      "<html><head><title>ESP Provisioning</title></head><body>"
      "<h1>配置 Wi-Fi</h1>"
      "<form method='POST' action='/submit'>"
      "SSID: <input name='ssid' maxlength='32' required><br>"
      "Password: <input name='password' type='password' maxlength='64'><br><br>"
      "<button type='submit'>保存</button>"
      "</form>"
      "</body></html>";
  _server.send(200, "text/html", page);
}

void ProvisioningManager::handleSubmit() {
  String ssid = _server.arg("ssid");
  String password = _server.arg("password");

  ssid.trim();
  password.trim();

  if (ssid.length() == 0 || ssid.length() > kMaxStoredSsidLength ||
      password.length() > kMaxStoredPasswordLength) {
    _server.send(400, "text/plain", "Invalid SSID or password length.");
    return;
  }

  memset(_pending.ssid, 0, sizeof(_pending.ssid));
  memset(_pending.password, 0, sizeof(_pending.password));
  ssid.toCharArray(_pending.ssid, sizeof(_pending.ssid));
  password.toCharArray(_pending.password, sizeof(_pending.password));
  _pending.valid = true;

  if (_store.save(_pending)) {
    _server.send(200, "text/html", "<html><body><h2>保存成功</h2><p>设备正在尝试连接新的 Wi-Fi，请断开此热点。</p></body></html>");
    _hasPending = true;
    stop();
  } else {
    _server.send(500, "text/plain", "保存凭据失败。");
  }
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
      _credentialStore(),
      _provisioningManager(_credentialStore),
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
