#include <Arduino.h>
#include <Car.h>
#include <DallasTemperature.h>
#include <Gps.h>
#include <OneWire.h>
#include <PsychicMqttClient.h>
#include <RFReceiver.h>
#include <WIFIManager.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#define PIN_LED 15

const char *ssid = "LinkRadio";
const char *password = "62724654";

const char *mqttUrl = "ws://mqtt.fieldlink.net.br";
const char *mqttUser = "esp32@troller";
const char *mqttPasswd = "pipa_papa3427";

String topic = "car/" + String(ESP.getEfuseMac());
String otaFirmwareUrl;

WIFIManager wifiManager;
Car car(33, 32, 27, 26, 15, 25, 36, 39);
PsychicMqttClient mqtt;
GPS gps;
RFReceiver rf(12);
OneWire onWire(2);
DallasTemperature sensors(&onWire);

void tempRun() {
  static unsigned long lastRequestMs = 0;
  static bool waitingConversion = false;

  static float lastTemp = NAN;

  const uint32_t interval = 3000; // intervalo entre leituras
  const uint32_t convTime = 750;  // tempo conversão (12 bits)

  unsigned long now = millis();

  // Serial.print("Sensores encontrados: ");
  // Serial.println(sensors.getDeviceCount());

  if (!waitingConversion && (now - lastRequestMs >= interval)) {
    sensors.requestTemperatures();
    lastRequestMs = now;
    waitingConversion = true;
  }

  if (waitingConversion && (now - lastRequestMs >= convTime)) {
    float temp = sensors.getTempCByIndex(0);
    waitingConversion = false;

    if (temp == DEVICE_DISCONNECTED_C)
      return;

    if (isnan(lastTemp) || temp != lastTemp) {
      lastTemp = temp;
      mqtt.publish((topic + "/temp").c_str(), 0, 0, String(temp).c_str());
    }
  }
}

void onMqttMessage(char *topic_, char *payload, int retain, int qos, bool dup) {
  Serial.printf("[MQTT]: topic: %s | qos: %d | dup: %d | retain: %d \n", topic_, qos, dup, retain);

  static unsigned long lastOtaCmdMs = 0;
  if (strcasecmp((topic + "/ota").c_str(), topic_) == 0) {
    if (millis() - lastOtaCmdMs < 15000) {
      mqtt.publish((topic + "/status").c_str(), 0, 0, "DEBOUNCE");
    }
    lastOtaCmdMs = millis();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      mqtt.publish((topic + "/ota/status").c_str(), 0, 0, "INVALID_JSON");
      return;
    }

    otaFirmwareUrl = doc["firmware"] | "";

    if (otaFirmwareUrl.isEmpty()) {
      mqtt.publish((topic + "/ota/status").c_str(), 0, 0, "EMPTY_PAYLOAD");
      return;
    }
  }

  if (strcasecmp((topic + "/cmd").c_str(), topic_) == 0) {
    if (strcasecmp(payload, "LOCK") == 0) {
      car.setState(State::LOCK);
    } else if (strcasecmp(payload, "UNLOCK") == 0) {
      car.setState(State::UNLOCK);
    } else if (strcasecmp(payload, "FUN") == 0) {
      car.setState(State::FUN);
    } else {
      mqtt.publish((topic + "/cmd/status").c_str(), 0, 0, "CMD UNKNOW");
    }
  }

  if (strcasecmp((topic + "/rf/cmd").c_str(), topic_) == 0) {
    if (strcasecmp(payload, "LEARN_ON") == 0) {
      rf.setLearn(true);
      mqtt.publish((topic + "/rf/status").c_str(), 0, false, "LEARN_ON");
    } else if (strcasecmp(payload, "LEARN_OFF") == 0) {
      rf.setLearn(false);
      mqtt.publish((topic + "/rf/status").c_str(), 0, false, "LEARN_OFF");
    } else {
      mqtt.publish((topic + "/rf/status").c_str(), 0, true, "WORK");
    }
  }
}

void handleMqtt() {
  gps.onUpdate([](TinyGPSPlus &gps) {
    JsonDocument doc;
    doc["lat"] = gps.location.lat();
    doc["lng"] = gps.location.lng();
    doc["sat"] = gps.satellites.value();
    doc["hdop"] = gps.hdop.hdop();
    doc["speed"] = gps.speed.kmph();
    doc["age"] = gps.location.age();

    String payload;
    serializeJson(doc, payload);

    mqtt.publish((topic + "/gps").c_str(), 0, 0, payload.c_str());
  });

  rf.onReceive([&](const RFCode &code) {
    char payload[32];
    snprintf(payload, sizeof(payload), "%lu", code.value);

    mqtt.publish((topic + "/rf/last_code").c_str(), 0, 0, payload);

    if (code.value == 174242312) {
      car.setState(State::FUN);
    }
  });

  car.onChange([](const State &state) {
    mqtt.publish((topic + "/state").c_str(), 0, 0, car.stateToString(state));
  });
}

void taskMqtt(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(2000));
  static bool mqttInitialized = false;
  static bool mqttConnected = false;

  for (;;) {
    bool wifiReady = wifiManager.isOnline();

    // Inicializa o MQTT apenas uma vez quando o WiFi ficar pronto
    if (wifiReady && !mqttInitialized) {
      Serial.println("[MQTT]: Starting MQTT.");

      mqtt.setServer(mqttUrl);
      if (strlen(mqttUser) > 0) {
        mqtt.setCredentials(mqttUser, mqttPasswd);
      }
      mqtt.onConnect([](bool sessionPresent) {
        String ip = WiFi.localIP().toString();
        Serial.println("[MQTT]: Connected in the broker!");

        mqtt.subscribe((topic + "/rf/cmd").c_str(), 0);
        mqtt.subscribe((topic + "/cmd").c_str(), 0);
        mqtt.subscribe((topic + "/ota").c_str(), 0);

        mqtt.publish((topic + "/status").c_str(), 0, 0, "ONLINE");
        mqtt.publish((topic + "/ip").c_str(), 0, true, ip.c_str());
      });

      // mqtt.subscribe((topic + "/rf/cmd").c_str(), 0);
      mqtt.onMessage(onMqttMessage);

      mqtt.setWill((topic + "/status").c_str(), 1, true, "OFFLINE");

      handleMqtt(); // registra callbacks do GPS, RF, Car

      mqtt.connect();

      mqttInitialized = true;
    }

    if (mqttInitialized) {

      vTaskDelay(pdMS_TO_TICKS(20000));

      bool nowConnected = mqtt.connected();
      if (nowConnected && !mqttConnected) {
        Serial.println("[MQTT]: Successful");
        mqttConnected = true;
      } else if (!nowConnected && mqttConnected) {
        Serial.println("[MQTT]: Connection lost.");
        mqttConnected = false;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms é bom para MQTT
  }
}

void taskIO(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(1000));
  while (true) {
    car.run();
    gps.run();
    rf.run();
    // tempRun();
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

void taskWIFIManager(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(500));
  while (true) {
    wifiManager.run();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {

  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 13, 14); // GPS
  delay(1000);

  esp_netif_init();

  car.begin();
  sensors.begin();
  rf.begin();

  gps.begin(&Serial1);
  gps.setUpdateInterval(10000);

  wifiManager.begin(WIFIMode::AUTO);

  xTaskCreatePinnedToCore(taskMqtt, "TaskMQTT", 8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(taskWIFIManager, "TaskWIFIManager", 8192, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(taskIO, "TaskIO", 6144, NULL, 3, NULL, 1);
}

void loop() {

  unsigned long now = millis();
  static unsigned long lastOtaUpdateMs = 0;
  if (otaFirmwareUrl.length() && now - lastOtaUpdateMs >= 20000) {
    otaFirmwareUrl = "";
    lastOtaUpdateMs = now;
    Serial.println("[MY OTA]: Received firmware update, starting update and rebooting...");
    wifiManager.updateOTA(otaFirmwareUrl);
  }

  static unsigned long lastRunMs = 0;
  if (now - lastRunMs >= 250) {
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    lastRunMs = now;
  }
}
