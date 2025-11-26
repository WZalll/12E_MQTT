#include "ProvisioningManager.h"
#include <functional>

namespace DeviceCore {

namespace {
constexpr const char* kProvisioningApSsid = "esp-sta";
constexpr size_t kMaxStoredSsidLength = 32;
constexpr size_t kMaxStoredPasswordLength = 64;
}

ProvisioningManager::ProvisioningManager(CredentialStore& store, const char* phone, const char* manualUrl)
    : _store(store),
      _server(80),
      _provisioning(false),
      _hasPending(false),
      _pending(),
      _maintenancePhone(phone),
      _userManualUrl(manualUrl) {}

void ProvisioningManager::begin() {
  if (_provisioning) {
    return;
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kProvisioningApSsid);

  setupRoutes();
  _server.begin();

  // 启动 DNS 服务器，将所有域名解析到 AP IP，实现强制门户 (Captive Portal)
  _dnsServer.start(53, "*", WiFi.softAPIP());

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
  _dnsServer.stop();
  _server.stop();
  WiFi.softAPdisconnect(true);
  _provisioning = false;
}

void ProvisioningManager::loop() {
  if (_provisioning) {
    _dnsServer.processNextRequest();
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
  String phone = _maintenancePhone ? _maintenancePhone : "暂无";
  String manualUrl = _userManualUrl ? _userManualUrl : "http://www.readme.com";

  String page = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>设备网络配置</title>
  <style>
    :root {
      --bg-color: #f0f2f5;
      --card-bg: #ffffff;
      --primary-color: #0056b3; /* Industrial Blue */
      --text-primary: #333333;
      --text-secondary: #666666;
      --border-color: #dcdcdc;
      --input-bg: #f9f9f9;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    }
    body {
      background-color: var(--bg-color);
      color: var(--text-primary);
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      margin: 0;
      padding: 20px;
    }
    .container {
      background: var(--card-bg);
      width: 100%;
      max-width: 400px;
      padding: 30px;
      border-radius: 8px;
      box-shadow: 0 4px 12px rgba(0,0,0,0.1);
      border-top: 4px solid var(--primary-color);
    }
    h1 {
      font-size: 20px;
      margin-top: 0;
      margin-bottom: 20px;
      color: var(--primary-color);
      text-transform: uppercase;
      letter-spacing: 0.5px;
      border-bottom: 1px solid var(--border-color);
      padding-bottom: 10px;
    }
    .info-group {
      margin-bottom: 20px;
      padding: 15px;
      background-color: #eef6fc;
      border-radius: 4px;
      font-size: 14px;
    }
    .info-item {
      margin-bottom: 8px;
      display: flex;
      justify-content: space-between;
    }
    .info-item:last-child {
      margin-bottom: 0;
    }
    .info-label {
      font-weight: 600;
      color: var(--text-secondary);
    }
    .info-value {
      font-weight: 500;
    }
    form {
      display: flex;
      flex-direction: column;
      gap: 15px;
    }
    label {
      font-size: 14px;
      font-weight: 600;
      margin-bottom: 4px;
      display: block;
    }
    input {
      width: 100%;
      padding: 10px;
      border: 1px solid var(--border-color);
      border-radius: 4px;
      background-color: var(--input-bg);
      font-size: 16px;
      box-sizing: border-box;
    }
    input:focus {
      border-color: var(--primary-color);
      outline: none;
    }
    button {
      background-color: var(--primary-color);
      color: white;
      border: none;
      padding: 12px;
      border-radius: 4px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: background-color 0.2s;
    }
    button:hover {
      background-color: #004494;
    }
    .footer {
      margin-top: 25px;
      font-size: 12px;
      color: var(--text-secondary);
      text-align: center;
      border-top: 1px solid var(--border-color);
      padding-top: 15px;
    }
    a {
      color: var(--primary-color);
      text-decoration: none;
    }
    a:hover {
      text-decoration: underline;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>设备网络配置</h1>
    
    <div class="info-group">
      <div class="info-item">
        <span class="info-label">维保电话:</span>
        <span class="info-value">)rawliteral" + phone + R"rawliteral(</span>
      </div>
      <div class="info-item">
        <span class="info-label">使用手册:</span>
        <span class="info-value"><a href=")rawliteral" + manualUrl + R"rawliteral(" target="_blank">点击查看</a></span>
      </div>
    </div>

    <form method="POST" action="/submit">
      <div>
        <label for="ssid">Wi-Fi 名称 (SSID)</label>
        <input type="text" id="ssid" name="ssid" required placeholder="输入 Wi-Fi 名称">
      </div>
      <div>
        <label for="password">Wi-Fi 密码</label>
        <input type="password" id="password" name="password" placeholder="输入 Wi-Fi 密码">
      </div>
      <button type="submit">保存并连接</button>
    </form>

    <div class="footer">
      <p>请确保输入正确的 2.4GHz 网络信息。</p>
      <p>设备 ID: )rawliteral" + String(ESP.getChipId(), HEX) + R"rawliteral(</p>
    </div>
  </div>
</body>
</html>
)rawliteral";
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

}  // namespace DeviceCore
