#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <functional>
#include <vector>
//
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>

struct WIFINetwork {
  String ssid;
  String passwd;
  int priority;
};

enum class ScanState {
  IDLE,
  STARTING,
  RUNNING,
  DONE,
  FAILED,
};

enum class WIFIMode {
  AUTO,
  STA_ONLY,
  AP_ONLY
};

class WIFIManager {
public:
  WIFIManager();
  ~WIFIManager();

  void begin(
      WIFIMode mode = WIFIMode::AUTO,
      const char *defaultApSsid = "esp32@troller",
      const char *defaultApPasswd = "1234567890");

  void setSTACredentials(const char *ssid, const char *password);
  bool tryConnectSTA();
  void startAP();

  bool isSTAConnected() const;
  IPAddress getIP() const;
  WIFIMode getCurrentMode() const;
  bool isOnline() const;

  void run();

  std::vector<WIFINetwork> getSavedNetworks() const;
  bool addNetwork(const String &ssid, const String &pass, int priority = 10);
  bool removeNetwork(const String &ssid);
  void clearAllNetworks();

  // OTA UPDATE MQTT
  bool updateOTA(const String &url);
  bool updateFileSystemOTA(const String &url);

private:
  Preferences _prefs;
  AsyncWebServer *server = nullptr;

  bool _serverInitialized = false;

  String _sta_ssid;
  String _sta_passwd;
  String _ap_ssid = "esp32@troller";
  String _ap_passwd = "1234567890";

  WIFIMode _currentMode = WIFIMode::AUTO;
  bool _isConnected = false;

  unsigned long _connectTimeoutMs = 20000;

  void createWebServer();

  void handleWIFIEvent(WiFiEvent_t event, WiFiEventInfo_t info);
  bool loadConfig();
  bool saveConfig();

  void handleRoot(AsyncWebServerRequest *request);
  void handleSave(AsyncWebServerRequest *request);
  void handleScan(AsyncWebServerRequest *request);
  void handleScanResult(AsyncWebServerRequest *request);

  std::vector<WIFINetwork> _networks;
  // void handleAddNetwork(AsyncWebServerRequest *request);
  // void handleDeleteNetwork(AsyncWebServerRequest *request);
  void handleGetNetworks(AsyncWebServerRequest *request);

  // SCAN
  ScanState _scanState = ScanState::IDLE;

  unsigned long _scanStartMs = 0;
  unsigned long _lastScanRequestMs = 0;

  const uint32_t SCAN_TIMEOUT = 20000;
  const uint32_t SCAN_COOLDOWN = 5000;

  bool _scanRequested = false;
};