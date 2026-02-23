#include <Arduino.h>
#include "HX711.h"
#include <WiFi.h>
#include <PubSubClient.h>

// ------------------- WiFi Credentials -------------------
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// ------------------- MQTT -------------------
const char* mqtt_server = "broker.hivemq.com";
const char* device_id = "ESP32_TamperGuard_001";

WiFiClient espClient;
PubSubClient client(espClient);

// ------------------- HX711 -------------------
#define DT 32
#define SCK 33

HX711 scale;

// ------------------- Calibration -------------------
float calibration_factor = 490.56;
float smoothed = 0.0;

// ------------------- Tamper Parameters -------------------
const float JUMP_PERCENT = 0.2;
const unsigned long JUMP_TIME_MS = 200;

// ------------------- State -------------------
float prev_weight = 0.0;
unsigned long prev_time = 0;

// ------------------- WiFi Setup -------------------
void setup_wifi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ------------------- MQTT Reconnect -------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect(device_id)) {
      Serial.println("Connected");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retry in 2 sec");
      delay(2000);
    }
  }
}

// ------------------- Send Alert -------------------
void sendTamperAlert(String type, float weight) {

  String topic = "tamperguard/" + String(device_id) + "/alert";

  String payload = "{";
  payload += "\"device_id\":\"" + String(device_id) + "\",";
  payload += "\"tamper_type\":\"" + type + "\",";
  payload += "\"weight\":" + String(weight, 2) + ",";
  payload += "\"timestamp\":" + String(millis());
  payload += "}";

  client.publish(topic.c_str(), payload.c_str());

  Serial.println("🚨 MQTT Alert Sent:");
  Serial.println(payload);
}

// ------------------- Setup -------------------
void setup() {

  Serial.begin(115200);
  delay(200);

  setup_wifi();
  client.setServer(mqtt_server, 1883);

  scale.begin(DT, SCK);
  scale.set_scale(calibration_factor);
  scale.tare();

  Serial.println("TamperGuard Ready");
  prev_time = millis();
}

// ------------------- Loop -------------------
void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (!scale.is_ready()) {
    Serial.println("⚠ HX711 not responding");
    delay(500);
    return;
  }

  // Read weight
  float rawSample = scale.get_units(1);
  smoothed = (smoothed * 0.8f) + (rawSample * 0.2f);

  if (fabs(smoothed) < 2.0f) smoothed = 0.0f;

  float current_weight = smoothed;
  unsigned long current_time = millis();
  unsigned long dt = current_time - prev_time;

  // -------- Rule A: Zero Manipulation --------
  long rawADC = scale.read();

  if (current_weight == 0.0f && llabs(rawADC) > 100) {
    Serial.println("⚠ Zero Manipulation Detected");
    sendTamperAlert("Zero Manipulation", current_weight);
  }

  // -------- Rule B: Sudden Jump --------
  float delta = fabs(current_weight - prev_weight);
  float compareBase = max(1.0f, fabs(prev_weight));

  if (dt > 0 && (delta > (JUMP_PERCENT * compareBase)) && dt < JUMP_TIME_MS) {
    Serial.println("⚠ Sudden Jump Detected");
    sendTamperAlert("Sudden Jump", current_weight);
  }

  // -------- Normal Output --------
  Serial.print("Weight: ");
  Serial.print(current_weight, 2);
  Serial.println(" g");

  prev_weight = current_weight;
  prev_time = current_time;

  delay(100);
}