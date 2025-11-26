#include "SerialForwarder.h"
#include <cstring>

namespace DeviceCore {

namespace {
constexpr size_t kDefaultSerialBufferLimit = 256;
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

}  // namespace DeviceCore
