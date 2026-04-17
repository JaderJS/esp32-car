#pragma once
#include <Arduino.h>

#define MAX_PULSES 6

struct Pulse {
  uint8_t pin;
  unsigned long startMs;
  uint16_t duration;
  bool active;
  bool inverted;
};

enum State {
  IDLE,
  LOCK,
  UNLOCK,
  ALARM,
  ERROR,
  FUN
};

class Car {
public:
  Car(
      uint8_t pinArrowLeft,
      uint8_t pinArrowRight,
      uint8_t pinLock,
      uint8_t pinUnlock,
      uint8_t pinCutOff,
      uint8_t pinBuzzer,
      uint8_t pinEngine,
      uint8_t pinCoolant) : _pinLedArrowLeft(pinArrowLeft),
                            _pinLedArrowRight(pinArrowRight),
                            _pinLock(pinLock),
                            _pinUnlock(pinUnlock),
                            _pinCutOff(pinCutOff),
                            _pinBuzzer(pinBuzzer),
                            _pinEngine(pinEngine),
                            _pinCoolant(pinCoolant) {
  }

  inline void begin() {
    pinMode(_pinLedArrowLeft, OUTPUT);
    pinMode(_pinLedArrowRight, OUTPUT);
    pinMode(_pinLock, OUTPUT);
    pinMode(_pinUnlock, OUTPUT);
    pinMode(_pinCutOff, OUTPUT);
    pinMode(_pinBuzzer, OUTPUT);

    pinMode(_pinCoolant, INPUT);
    pinMode(_pinEngine, INPUT);

    idleIO();

    _state = IDLE;

    initPulses();
  }

  inline void run() {
    unsigned long now = millis();
    static unsigned long lastErrorMs = 0;
    static unsigned long lastErrorVerificationMs = 0;

    if (_verifyErros && !isHealthy() && (now - lastErrorVerificationMs > 25000)) {
      setState(State::ERROR);
    }

    handlePulses(now);

    switch (_state) {
    case State::LOCK:
      lock();
      setState(State::IDLE);
      break;
    case State::UNLOCK:
      unlock();
      setState(State::IDLE);
      break;
    case ERROR:
      if (!_error.isEmpty() && now - lastErrorMs > 1000) {
        Serial.printf("%s\n", _error.c_str());
        lastErrorMs = now;
      }
      Serial.println("[CAR]: Not found error.");
      break;
    case State::FUN:
      pulse(_pinLedArrowLeft, 600);
      pulse(_pinLedArrowRight, 600);

      setState(State::IDLE);

      break;

    default:
      setState(State::IDLE);
      break;
    }
  }

  inline void setState(State state) {

    if (_state == state) return;

    _state = state;

    if (_onChange) {
      _onChange(_state);
    }
  }

  inline void onChange(std::function<void(State)> cb) {
    _onChange = cb;
  }

  inline const char *stateToString(State s) {
    static const char *map[] = {
        "IDLE",
        "LOCK",
        "UNLOCK",
        "ALARM",
        "ERROR"};

    if (s >= 0 && s <= ERROR)
      return map[s];

    return "UNKNOWN";
  }

  inline void setVerifyErrors(bool s) {
    _verifyErros = s;
  }

private:
  State _state;

  uint8_t _pinLedArrowLeft;  // Arrow rigth
  uint8_t _pinLedArrowRight; // Arrow left
  uint8_t _pinCutOff;        // cut engine
  uint8_t _pinLedStatus;     // info status car using one led
  uint8_t _pinLock;          // pulse to 50ms to lock
  uint8_t _pinUnlock;        // pulse to 50ms to unlock
  uint8_t _pinBuzzer;        // use buzzer to status sound
  uint8_t _pinEngine;        // read to pos-engine status
  uint8_t _pinCoolant;

  bool _verifyErros = false;
  String _error;

  std::function<void(State)> _onChange = nullptr;

  Pulse _pulse = {0, 0, 50, false, false};
  Pulse _pulses[MAX_PULSES];

  void pulse(uint8_t pin, uint16_t duration = 50, bool inverted = false) {

    for (int i = 0; i < MAX_PULSES; i++) {
      if (_pulses[i].active && _pulses[i].pin == pin) {
        return;
      }
    }

    for (int i = 0; i < MAX_PULSES; i++) {
      if (!_pulses[i].active) {
        digitalWrite(pin, inverted ? 0 : 1);

        _pulses[i] = {
            pin,
            millis(),
            duration,
            true,
            inverted,
        };
        return;
      }
    }
  }

  void handlePulses(unsigned long now) {

    for (int i = 0; i < MAX_PULSES; i++) {

      if (!_pulses[i].active) continue;

      if (now - _pulses[i].startMs >= _pulses[i].duration) {

        digitalWrite(
            _pulses[i].pin,
            _pulses[i].inverted ? HIGH : LOW);

        _pulses[i].active = false;
      }
    }
  }

  bool isEngineOn() {
    return digitalRead(_pinEngine);
  }

  bool isHealthy() {
    bool cool = digitalRead(_pinCoolant);
    if (cool) {
      _error += "[CAR]: CAUTION! Coolant is low! \t";
      return false;
    }
    return true;
  }

  void lock() {
    pulse(_pinLock, 50, true);
    pulse(_pinLedArrowLeft, 300);
    pulse(_pinLedArrowRight, 300);
  }

  void unlock() {
    pulse(_pinUnlock, 50, true);
    pulse(_pinLedArrowLeft, 300);
    pulse(_pinLedArrowRight, 300);
  }

  void initPulses() {
    for (int i = 0; i < MAX_PULSES; i++) {
      _pulses[i].active = false;
    }
  }

  void idleIO() {
    digitalWrite(_pinLedArrowLeft, LOW);
    digitalWrite(_pinLedArrowRight, LOW);
    digitalWrite(_pinLock, LOW);
    digitalWrite(_pinUnlock, LOW);
    digitalWrite(_pinCutOff, LOW);
    digitalWrite(_pinBuzzer, LOW);
  }
};