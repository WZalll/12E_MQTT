#include <Arduino.h>
#include <DeviceCore.h>

using DeviceCore::DeviceConfig;
using DeviceCore::DeviceController;

namespace {

DeviceConfig kDeviceConfig = {
  "OP11",                         // ssid
  "88888888",                    // password
  "broker.emqx.io",              // mqttServer
  1883,                           // mqttPort
  "mah1ro_esp32",                // clientId
  "esp32/test/mah1ro",           // primaryTopic
  "esp32/test/mah1ro/serial",    // serialTopic
  2,                              // pinErr (active-low)
  5,                              // pinUser1 (active-low)
  5000UL,                         // heartbeatInterval
  150UL,                          // user1PulseDuration
  150UL,                          // errPulseDuration
  256,                            // serialBufferLimit
  115200UL                        // serialBaud
};

DeviceController controller(kDeviceConfig);

}  // namespace

void setup() {
  controller.begin();
}

void loop() {
  controller.loop();
}

