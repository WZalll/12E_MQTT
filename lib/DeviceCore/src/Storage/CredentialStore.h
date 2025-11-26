#pragma once

#include <Arduino.h>
#include <EEPROM.h>

namespace DeviceCore {

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

}  // namespace DeviceCore
