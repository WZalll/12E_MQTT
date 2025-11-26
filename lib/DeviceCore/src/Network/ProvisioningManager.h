#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include "../Storage/CredentialStore.h"

namespace DeviceCore {

class ProvisioningManager {
public:
  ProvisioningManager(CredentialStore& store, const char* phone = nullptr, const char* manualUrl = nullptr);

  void begin();
  void stop();
  void loop();
  bool isProvisioning() const;
  bool hasNewCredentials() const;
  StoredCredentials consumeCredentials();

private:
  CredentialStore& _store;
  ESP8266WebServer _server;
  DNSServer _dnsServer;
  bool _provisioning;
  bool _hasPending;
  StoredCredentials _pending;
  const char* _maintenancePhone;
  const char* _userManualUrl;

  void setupRoutes();
  void handleRoot();
  void handleSubmit();
};

}  // namespace DeviceCore
