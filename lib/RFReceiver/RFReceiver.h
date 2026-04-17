#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <RCSwitch.h>
#include <functional>
#include <vector>

struct RFCode {
  uint32_t value;
  unsigned int bitLength;
  unsigned int protocol;
};

class RFReceiver {

public:
  RFReceiver(uint8_t pin) : _pin(pin) {
  }

  inline void begin() {
    _rf.enableReceive(digitalPinToInterrupt(_pin));

    if (!LittleFS.begin(false)) {
      Serial.println("[RF] LittleFS mount failed");
      return;
    }

    loadWhiteList();
  }

  inline void run() {
    if (!_rf.available())
      return;

    RFCode code = {
        _rf.getReceivedValue(),
        _rf.getReceivedBitlength(),
        _rf.getReceivedProtocol()};

    _rf.resetAvailable();

    if (_learnMode) {
      addToWhitelist(code.value);
      Serial.printf("[RF]: Learned: %lu\n", code.value);
    }

    if (!isValid(code)) {
      return;
    }

    unsigned long now = millis();
    if (_debounceMs > 0 && (now - _lastTimeDebounceMs < _debounceMs)) {
      return;
    }
    _lastCode = code;
    _lastTimeDebounceMs = now;

    if (_onReceive) {
      _onReceive(code);
    }
  }

  inline void onReceive(std::function<void(const RFCode &)> cb) {
    _onReceive = cb;
  }

  inline void addToWhitelist(uint32_t value) {
    for (auto v : _whiteList) {
      if (v == value)
        return;
    }

    _whiteList.push_back(value);
    saveWhitelist();
  }

  inline void clearWhitelist() {
    _whiteList.clear();
    LittleFS.remove("/rf.json");
  }

  inline void setLearn(bool s) {
    _learnMode = s;
  }

private:
  RCSwitch _rf;
  uint8_t _pin;
  bool _learnMode = false;

  std::vector<uint32_t> _whiteList;

  std::function<void(const RFCode &)> _onReceive = nullptr;

  unsigned long _lastTimeDebounceMs = 0;
  uint32_t _debounceMs = 300;
  RFCode _lastCode = {0, 0, 0};
  uint8_t _minBitLength = 20;

  bool isValid(const RFCode &code) {
    if (code.value == 0 || code.value == 0xFFFFFFFF) return false;

    if (code.bitLength < _minBitLength) return false;

    // if (!_whiteList.empty()) {
    //   for (auto v : _whiteList) {
    //     if (v == code.value)
    //       return true;
    //   }
    //   return false;
    // }

    return true;
  }

  void saveWhitelist() {
    JsonDocument doc;

    JsonArray arr = doc.createNestedArray("whitelist");

    for (auto v : _whiteList) {
      arr.add(v);
    }

    File f = LittleFS.open("/rf.json", "w");
    if (!f) {
      Serial.println("[RF] fail open rf.json for write");
      return;
    }

    serializeJsonPretty(doc, f);
    f.close();

    Serial.println("[RF] whitelist saved");
  }

  void loadWhiteList() {
    _whiteList.clear();

    if (!LittleFS.exists("/rf.json")) {
      Serial.println("[RF]: rf.json not found");
      return;
    }

    File file = LittleFS.open("/rf.json", "r");
    if (!file) {
      Serial.println("[RF]: fail open rf.json");
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);

    if (error) {
      Serial.println("[RF]: JSON parse error");
      return;
    }

    JsonArray arr = doc["whitelist"].as<JsonArray>();

    for (JsonVariant v : arr) {
      uint32_t val = v.as<uint32_t>();
      if (val != 0) {
        _whiteList.push_back(val);
      }
    }
    Serial.printf("[RF]: %d codes loaded\n", _whiteList.size());
  }
};
