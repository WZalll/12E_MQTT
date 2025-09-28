#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <cstring>

const uint8_t PIN_ERR   = 2;   // active-low
const uint8_t PIN_USER1 = 5;   // active-low

const unsigned long HEARTBEAT_INTERVAL    = 5000UL;
const unsigned long USER1_PULSE_DURATION  = 150UL;
const unsigned long ERR_PULSE_DURATION    = 150UL;

const char* ssid           = "OP11";
const char* password       = "88888888";
const char* mqtt_server    = "broker.emqx.io";
const int   mqtt_port      = 1883;
const char* mqtt_client_id = "mah1ro_esp32";
const char* mqtt_topic     = "esp32/test/mah1ro";
const char* mqtt_serial_topic = "esp32/test/mah1ro/serial";

WiFiClient espClient;
PubSubClient client(espClient);

String serialBuffer;
const size_t SERIAL_BUFFER_LIMIT = 256;

bool heartbeat_enable = true;

unsigned long lastHeartbeatMs   = 0;
unsigned long user1PulseStartMs = 0;
bool          user1PulseActive  = false;
bool          errPulseActive    = false;
unsigned long errPulseStartMs   = 0;
unsigned long lastMqttRetryMs   = 0;
unsigned long lastWifiRetryMs   = 0;

void startUser1Pulse(unsigned long now) {
  digitalWrite(PIN_USER1, LOW);
  user1PulseActive = true;
  user1PulseStartMs = now;
}

void handleUser1Pulse(unsigned long now) {
  if (user1PulseActive && now - user1PulseStartMs >= USER1_PULSE_DURATION) {
    digitalWrite(PIN_USER1, HIGH);
    user1PulseActive = false;
  }
}

void startErrPulse(unsigned long now) {
  digitalWrite(PIN_ERR, LOW);
  errPulseActive = true;
  errPulseStartMs = now;
}

void updateErrLed(unsigned long now) {
  if (errPulseActive) {
    if (now - errPulseStartMs >= ERR_PULSE_DURATION) {
      errPulseActive = false;
    } else {
      return;
    }
  }
  bool networkReady = (WiFi.status() == WL_CONNECTED) && client.connected();
  digitalWrite(PIN_ERR, networkReady ? HIGH : LOW);
}

void publishHeartbeat() {
  String msg = "ESP heartbeat: ";
  msg += millis() / 1000;
  client.publish(mqtt_topic, msg.c_str());
  Serial.print("Published: ");
  Serial.println(msg);
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    msg += (char)payload[i];
  }
  Serial.println();
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, msg) == DeserializationError::Ok) {
    if (doc["cmd"] == "heartbeat") {
      heartbeat_enable = doc["enable"];
      Serial.print("[MQTT] Heartbeat switched to: ");
      Serial.println(heartbeat_enable ? "ON" : "OFF");
    }
  }
}

bool tryConnectMqtt() {
  Serial.print("Attempting MQTT connection...");
  if (client.connect(mqtt_client_id)) {
    Serial.println("connected");
    client.subscribe(mqtt_topic);
    Serial.print("Subscribed to topic: ");
    Serial.println(mqtt_topic);
    client.publish(mqtt_topic, "Hello from ESP32!");
    Serial.println("MQTT: Published 'Hello from ESP32!'");
    return true;
  }
  Serial.print("failed, rc=");
  Serial.println(client.state());
  return false;
}

bool publishMessage(const char* topic, const String& payload) {
  if (!topic || topic[0] == '\0') {
    return false;
  }
  return client.publish(topic, payload.c_str());
}

// 新增串口缓冲刷新函数，统一处理成功/失败
void flushSerialBuffer(unsigned long now) {
  if (serialBuffer.length() == 0) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Serial forward skipped: WiFi not connected.");
    startErrPulse(now);
  } else if (!client.connected()) {
    Serial.println("Serial forward skipped: MQTT not connected.");
    startErrPulse(now);
  } else {
    bool serialOk = publishMessage(mqtt_serial_topic, serialBuffer);
    bool sameTopic = (mqtt_serial_topic && mqtt_topic && strcmp(mqtt_serial_topic, mqtt_topic) == 0);
    bool primaryOk = serialOk && sameTopic ? true : publishMessage(mqtt_topic, serialBuffer);
    if (serialOk || primaryOk) {
      if (!serialOk && primaryOk) {
        Serial.println("Serial topic publish failed, mirrored via primary topic.");
      }
      Serial.print("Forwarded serial: ");
      Serial.println(serialBuffer);
      startUser1Pulse(now);
    } else {
      Serial.println("Serial forward failed: MQTT publish error.");
      startErrPulse(now);
    }
  }
  serialBuffer = "";
}

void forwardSerialIfNeeded(unsigned long now) {
  static bool escapePending = false;
  while (Serial.available() > 0) {
    char ch = static_cast<char>(Serial.read());

    if (escapePending) {
      if (ch == 'n' || ch == 'N' || ch == 'r' || ch == 'R') {
        flushSerialBuffer(now);
      } else {
        if (serialBuffer.length() < SERIAL_BUFFER_LIMIT) {
          serialBuffer += '\\';
        }
        if (serialBuffer.length() < SERIAL_BUFFER_LIMIT) {
          serialBuffer += ch;
        }
      }
      escapePending = false;
      continue;
    }

    if (ch == '\\') {
      escapePending = true;
      continue;
    }

    if (ch == '\r' || ch == '\n') {
      flushSerialBuffer(now);
      continue;
    }

    if (serialBuffer.length() < SERIAL_BUFFER_LIMIT) {
      serialBuffer += ch;
    }
  }
}

void setup() {
  Serial.begin(115200);
  serialBuffer.reserve(SERIAL_BUFFER_LIMIT);
  pinMode(PIN_USER1, OUTPUT);
  pinMode(PIN_ERR, OUTPUT);
  digitalWrite(PIN_USER1, HIGH);
  digitalWrite(PIN_ERR, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
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

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  while (!tryConnectMqtt()) {
    delay(2000);
  }
  updateErrLed(millis());
  lastHeartbeatMs = millis();
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiRetryMs >= 2000) {
      Serial.println("WiFi disconnected, retrying...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      lastWifiRetryMs = now;
    }
  } else if (!client.connected()) {
    if (now - lastMqttRetryMs >= 2000) {
      Serial.println("MQTT disconnected, retrying...");
      if (tryConnectMqtt()) {
        lastHeartbeatMs = now;
      }
      lastMqttRetryMs = now;
    }
  } else {
    client.loop();
    if (heartbeat_enable && now - lastHeartbeatMs >= HEARTBEAT_INTERVAL) {
      publishHeartbeat();
      startUser1Pulse(now);
      lastHeartbeatMs = now;
    }
  }

  forwardSerialIfNeeded(now);
  handleUser1Pulse(now);
  updateErrLed(now);
  delay(10);
}

