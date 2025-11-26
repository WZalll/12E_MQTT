#include "CredentialStore.h"

namespace DeviceCore {

namespace {
constexpr size_t kMaxStoredSsidLength = 32;
constexpr size_t kMaxStoredPasswordLength = 64;
constexpr size_t kCredentialEepromSize = 1 + kMaxStoredSsidLength + kMaxStoredPasswordLength;
constexpr uint8_t kCredentialMagic = 0xA5;
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

}  // namespace DeviceCore
