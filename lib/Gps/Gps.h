#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <functional>

class GPS {
public:
  void begin(Stream *stream) {
    _stream = stream;
  }

  inline bool run() {
    bool hasNewData = false;
    while (_stream && _stream->available()) {
      if (_gps.encode(_stream->read())) {
        hasNewData = true;
        unsigned long now = millis();
        if (_onUpdate && (now - _lastUpdate >= _minUpdateInterval)) {
          _lastUpdate = now;
          _onUpdate(_gps);
        }
      }
    }
    return hasNewData;
  }

  inline void onUpdate(std::function<void(TinyGPSPlus &)> cb) {
    _onUpdate = cb;
  }

  inline bool isFix() {
    return _gps.location.isValid() && _gps.location.age() < 2000;
  }

  inline void setUpdateInterval(unsigned long time) {
    _minUpdateInterval = time;
  }

private:
  Stream *_stream = nullptr;
  TinyGPSPlus _gps;
  std::function<void(TinyGPSPlus &)> _onUpdate = nullptr;
  unsigned long _minUpdateInterval = 1000;
  unsigned long _lastUpdate = 0;
};