#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "../Config/DeviceConfig.h"
#include "../Storage/CredentialStore.h"
#include "../Network/ProvisioningManager.h"
#include "../Network/MqttLayer.h"
#include "../Hardware/LedSubsystem.h"
#include "../Hardware/SerialForwarder.h"

namespace DeviceCore {

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
