#include "WIFIManager.h"
#include <ElegantOTA.h>

WIFIManager::WIFIManager() {
}

WIFIManager::~WIFIManager() {
  if (server) {
    server->end();
    delete server;
  };
}

bool WIFIManager::loadConfig() {
  if (!LittleFS.exists("/config.json")) {
    Serial.println("[WIFI MANAGER]: config.json not founded. Create new file.");
    return saveConfig();
  }

  File file = LittleFS.open("/config.json", "r");
  if (!file) return false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("[WIFI MANAGER]: Erro to read JSON");
    return false;
  }

  _networks.clear();

  JsonArray nets = doc["networks"].as<JsonArray>();
  for (JsonObject n : nets) {
    WIFINetwork net;
    net.ssid = n["ssid"].as<String>();
    net.passwd = n["pass"].as<String>();
    net.priority = n["priority"] | 10;

    if (!net.ssid.isEmpty()) {
      _networks.push_back(net);
    }
  }

  _ap_ssid = doc["ap_ssid"] | "esp32@troller";
  _ap_passwd = doc["ap_pass"] | "1234567890";

  Serial.printf("[WIFI MANAGER]: %d networks loaded to config.json\n", _networks.size());

  return true;
}

bool WIFIManager::saveConfig() {
  JsonDocument doc;

  JsonArray nets = doc.createNestedArray("networks");
  for (const auto &net : _networks) {
    JsonObject obj = nets.add<JsonObject>();
    obj["ssid"] = net.ssid;
    obj["pass"] = net.passwd;
    obj["priority"] = net.priority;
  }

  doc["ap_ssid"] = _ap_ssid;
  doc["ap_pass"] = _ap_passwd;

  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println("[WIFI MANAGER]: Erro to open config.json for writer.");
    return false;
  }

  serializeJsonPretty(doc, file);
  serializeJsonPretty(doc, Serial);
  file.close();

  Serial.println("[WIFI MANAGER]: Credential save to successfully LittleFS!");
  return true;
}

bool WIFIManager::addNetwork(const String &ssid, const String &pass, int priority) {
  if (ssid.isEmpty() || pass.isEmpty()) return false;

  removeNetwork(ssid);

  WIFINetwork net;
  net.ssid = ssid;
  net.passwd = pass;
  net.priority = priority;

  _networks.push_back(net);

  bool saved = saveConfig();
  if (saved) {
    Serial.printf("[WIFI MANAGER]: Network '%s' include with success %d\n", ssid.c_str(), priority);
  }
  return saved;
}

bool WIFIManager::removeNetwork(const String &ssid) {
  for (auto it = _networks.begin(); it != _networks.end(); ++it) {
    if (it->ssid.equalsIgnoreCase(ssid)) {
      _networks.erase(it);
      return saveConfig();
    }
  }
  return false;
}

void WIFIManager::clearAllNetworks() {
  _networks.clear();
  saveConfig();
}

void WIFIManager::handleWIFIEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Serial.printf("[WIFI MANAGER]: IP %s \n", WiFi.localIP().toString().c_str());
    if (MDNS.begin("esp32")) {
      Serial.println("[WIFI MANAGER @mDNS] iniciado: http://esp32.local");
    } else {
      Serial.println("[WIFI MANAGER @mDNS] falhou");
    }
    _isConnected = true;
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Serial.println("[WIFI MANAGER]: Disconnect STA. Trying reconecting...");
    _isConnected = false;
    if (!_sta_ssid.isEmpty()) {
      WiFi.reconnect();
    }
    break;
  default:
    break;
  }
}

void WIFIManager::createWebServer() {

  if (_serverInitialized) return;

  if (!server) {
    server = new AsyncWebServer(80);
  }

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html");
  });

  server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handleSave(request);
  });

  server->on("/scan/start", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleScan(request);
  });

  server->on("/scan/result", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleScanResult(request);
  });

  server->on("/api/networks", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleGetNetworks(request);
  });

  server->on("/networks", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/view_networks.html");
  });

  // server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
  //   request->send(LittleFS, "/style.css", "text/css");
  // });

  // server->serveStatic("/", LittleFS, "/")
  //     .setDefaultFile("index.html")
  //     .setCacheControl("no-cache, no-store, must-revalidate");

  server->onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  server->begin();

  ElegantOTA.begin(server);

  ElegantOTA.onStart([]() {
    Serial.println("[WIFI MANAGER @OTA]: OTA update process started.");
  });

  static unsigned long otaProgressMs = 0;
  ElegantOTA.onProgress([](size_t current, size_t final) {
    if (millis() - otaProgressMs > 1000) {
      Serial.printf("[WIFI MANAGER @OTA]: Progress: %u%%\n", (current * 100) / final);
    }
  });

  ElegantOTA.onEnd([](bool success) {
    if (success) {
      Serial.println("[WIFI MANAGER @OTA]: Update completed successfully.");
      return;
    }
    Serial.println("[WIFI MANAGER @OTA]: OTA update failed.");
  });

  _serverInitialized = true;
}

void WIFIManager::begin(WIFIMode mode, const char *defaultApSsid, const char *defaultApPasswd) {

  _currentMode = mode;
  if (defaultApSsid) _ap_ssid = defaultApSsid;
  if (defaultApPasswd) _ap_passwd = defaultApPasswd;

  if (!LittleFS.begin(true)) {
    Serial.println("[WIFI MANAGER]: Fail mounted LittleFS!");
    return;
  }

  loadConfig();

  WiFi.onEvent(std::bind(&WIFIManager::handleWIFIEvent, this, std::placeholders::_1, std::placeholders::_2));
  Serial.println("[WIFI MANAGER]: AsyncWeb started");

  createWebServer();

  bool staSuccess = false;
  if ((mode == WIFIMode::AUTO || mode == WIFIMode::STA_ONLY) && !_networks.empty()) {
    staSuccess = tryConnectSTA();
  }

  if (!staSuccess || mode == WIFIMode::AP_ONLY) {
    startAP();
  } else {
    Serial.println("[WIFI MANAGER]: Conectado em STA com sucesso.");
  }
}

bool WIFIManager::tryConnectSTA() {
  if (_networks.empty()) {
    Serial.println("[WIFI MANAGER]: Empty networks.");
    return false;
  }

  auto sorted = _networks;
  std::sort(sorted.begin(), sorted.end(), [](const WIFINetwork &a, const WIFINetwork &b) { return a.priority < b.priority; });

  for (const auto &net : sorted) {
    Serial.printf("[WIFI MANAGER]: Trying - %s - order (%d)\n", net.ssid.c_str(), net.priority);
    WiFi.mode(WIFI_STA);
    WiFi.begin(net.ssid.c_str(), net.passwd.c_str());

    Serial.println();
    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs <= _connectTimeoutMs)) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI MANAGER] Conectado! SSID: %s | IP: %s\n", net.ssid.c_str(), WiFi.localIP().toString().c_str());
      _isConnected = true;
      return true;
    }
  }

  Serial.println("[WIFI MANAGER]: Fail to connect save networks");
  return false;
}

void WIFIManager::startAP() {
  Serial.println("[WIFI MANAGER]: Starting AP mode");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(_ap_ssid.c_str(), _ap_passwd.c_str());

  Serial.printf("[WIFI MANAGER]: AP + Web Server running!\n");
  Serial.printf("[WIFI MANAGER]: SSID: %s | Senha: %s | IP: %s\n", _ap_ssid.c_str(), _ap_passwd.c_str(), WiFi.softAPIP().toString().c_str());
}

void WIFIManager::handleRoot(AsyncWebServerRequest *request) {
  request->send(LittleFS, "/index.html", "text/html");
}

void WIFIManager::handleSave(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true)) {
    String ssid = request->getParam("ssid", true)->value();
    String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
    addNetwork(ssid, pass);
  }
  request->send(200, "text/plain", "Credenciais salvas! Reiniciando...");
  delay(1000);
  ESP.restart();
}

void WIFIManager::handleScan(AsyncWebServerRequest *request) {
  unsigned long now = millis();

  if (_scanState == ScanState::RUNNING || _scanState == ScanState::STARTING) {
    request->send(200, "application/json", "{\"status\":\"running\"}");
    return;
  }

  if (now - _lastScanRequestMs < SCAN_COOLDOWN) {
    request->send(200, "application/json", "{\"status\":\"cooldown\"}");
    return;
  }

  _scanRequested = true;
  _lastScanRequestMs = now;

  request->send(200, "application/json", "{\"status\":\"started\"}");
}

void WIFIManager::handleScanResult(AsyncWebServerRequest *request) {

  if (_scanState == ScanState::RUNNING || _scanState == ScanState::STARTING) {
    request->send(200, "application/json", "{\"status\":\"running\"}");
    return;
  }

  if (_scanState == ScanState::FAILED) {
    request->send(200, "application/json", "{\"status\":\"failed\"}");
    return;
  }

  int n = WiFi.scanComplete();

  if (n <= 0) {
    request->send(200, "application/json", "[]");
    return;
  }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < n; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
  }

  WiFi.scanDelete();
  _scanState = ScanState::IDLE;

  String response;
  serializeJson(doc, response);

  request->send(200, "application/json", response);
}

void WIFIManager::handleGetNetworks(AsyncWebServerRequest *request) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (const auto &net : _networks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = net.ssid;
    obj["priority"] = net.priority;
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

bool WIFIManager::isSTAConnected() const {
  return _isConnected && WiFi.status() == WL_CONNECTED;
}

IPAddress WIFIManager::getIP() const {
  return (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
}

WIFIMode WIFIManager::getCurrentMode() const {
  return _currentMode;
}

void WIFIManager::setSTACredentials(const char *ssid, const char *password) {
  addNetwork(ssid, password);
}

bool WIFIManager::isOnline() const {
  if (_currentMode == WIFIMode::AP_ONLY) return false;

  if (WiFi.getMode() & WIFI_STA) {
    return (WiFi.status() == WL_CONNECTED) && _isConnected && (WiFi.localIP() != IPAddress(0, 0, 0, 0));
  }
  return false;
}

void WIFIManager::run() {

  unsigned long now = millis();
  int status;

  switch (_scanState) {
  case ScanState::IDLE: {

    if (!_scanRequested) return;

    _scanRequested = false;

    Serial.println("[WIFI MANAGER @SCAN]: starting...");

    WiFi.scanDelete();
    delay(10);
    WiFi.scanNetworks(true);

    _scanStartMs = now;
    _scanState = ScanState::RUNNING;
    break;
  }

  case ScanState::RUNNING:
    status = WiFi.scanComplete();
    if (status < 0) {
      if ((now - _scanStartMs) > SCAN_TIMEOUT) {
        Serial.println("[WIFI MANAGER @SCAN]: Scan failed with timeout");
        _scanState = ScanState::FAILED;
        WiFi.scanDelete();
        return;
      }
    }

    if (status >= 0) {
      Serial.printf("[WIFI MANAGER @SCAN]: Done (%d networks)\n", status);
      _scanState = ScanState::DONE;
    }
    break;

  case ScanState::DONE:
    break;
  case ScanState::FAILED:
    break;

  default:
    _scanState = ScanState::IDLE;
    break;
  }
  ElegantOTA.loop();
}