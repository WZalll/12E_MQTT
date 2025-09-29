#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>

#include <EEPROM.h>

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
};

struct StoredCredentials {
  StoredCredentials();
  char ssid[33];
  char password[65];
  bool valid;
};

class CredentialStore {
public:
  CredentialStore();

  bool begin();
  bool load(StoredCredentials& out);
  bool save(const StoredCredentials& creds);
  void clear();

private:
  bool _initialized;
};

class ProvisioningManager {
public:
  explicit ProvisioningManager(CredentialStore& store);

  void begin();
  void stop();
  void loop();
  bool isProvisioning() const;
  bool hasNewCredentials() const;
  StoredCredentials consumeCredentials();

private:
  CredentialStore& _store;
  ESP8266WebServer _server;
  bool _provisioning;
  bool _hasPending;
  StoredCredentials _pending;

  void setupRoutes();
  void handleRoot();
  void handleSubmit();
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
  CredentialStore _credentialStore;
  ProvisioningManager _provisioningManager;
  StoredCredentials _credentials;
  bool _heartbeatEnabled;
  unsigned long _lastWifiRetryMs;
  unsigned long _lastProvisioningCheckMs;
  unsigned long _resetPressStartMs;
  bool _resetTriggered;
  bool _wifiReady;
  char _ssidBuffer[33];
  char _passwordBuffer[65];
  const char* _initialSsid;
  const char* _initialPassword;

  static void mqttCallback(char* topic, byte* payload, unsigned int length);

  void onMqttMessage(char* topic, byte* payload, unsigned int length);

  void ensureWifiConnected(unsigned long now);
  void initializeCredentials();
  void startProvisioning();
  void stopProvisioning();
  void handleProvisioning(unsigned long now);
  void handleResetButton(unsigned long now);
  bool connectWifi();
  void applyCredentials(const StoredCredentials& creds);
  void clearCredentials();
};

}  // namespace DeviceCore
