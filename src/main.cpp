#include <Arduino.h>
#include "HX711.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <IRremote.h> 
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ------------------- WiFi Credentials -------------------
const char* ssid = "Vishaal 's 1plus";
const char* password = "vishaal@123?";

// ------------------- MQTT -------------------
const char* mqtt_server = "test.mosquitto.org"; 
const char* device_id = "ESP32_TamperGuard_001";

WiFiClient espClient;
PubSubClient client(espClient);

// ------------------- Hardware Pins -------------------
#define DT 32
#define SCK 33
#define IR_RECEIVE_PIN 15 

LiquidCrystal_I2C lcd(0x27, 16, 2); 

HX711 scale;

// ------------------- Calibration -------------------
float calibration_factor = 400.0;
float smoothed_pure = 0.0; // ONLY tracks real physical weight

// ------------------- Tamper Parameters -------------------
const float LIFT_THRESHOLD = -15.0;        
const unsigned long SETTLE_TIME_MS = 2000; 
const float MOTION_THRESHOLD = 1.0;        

// ------------------- State Variables -------------------
float prev_pure_weight = 0.0;
unsigned long motion_start_time = 0;
unsigned long last_motion_time = 0;        
bool is_moving = false;
unsigned long last_hardware_check = 0;

// This is kept strictly separate from physical calculations
float simulated_tamper_offset = 0.0; 

// --- LCD/Serial Display State ---
String lcd_status = "Secure";
unsigned long last_alert_time = 0;

// --- SPAM PREVENTION LATCHES ---
bool tray_lifted_state = false;        
bool continuous_alert_sent = false;    

// --- HARDCODED IR COMMANDS ---
const uint16_t ir_cmd_add = 32;
const uint16_t ir_cmd_sub = 33;
const uint16_t ir_cmd_tare = 82; // Updated to your new Tare button code!

// ------------------- WiFi Setup -------------------
void setup_wifi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi.");
  Serial.print("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connected! ");
  Serial.println("\nWiFi Connected");
  delay(1000);
}

// ------------------- MQTT Reconnect -------------------
void reconnect() {
  while (!client.connected()) {
    lcd.setCursor(0, 1);
    lcd.print("Conn. Broker... ");
    Serial.print("Connecting to MQTT...");
    
    if (client.connect(device_id)) {
      lcd.setCursor(0, 1);
      lcd.print("MQTT Connected! ");
      Serial.println("Connected");
      delay(1000);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
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
  
  lcd_status = type;
  last_alert_time = millis();
  
  Serial.println("\n🚨 MQTT ALERT FIRED: " + type);
}

// ------------------- Setup -------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("TamperGuard V3");
  Serial.println("====================================");
  Serial.println("  TAMPERGUARD STARTING (ISOLATED MODE)");
  Serial.println("====================================");
  delay(1500);

  setup_wifi();
  client.setServer(mqtt_server, 1883);

  scale.begin(DT, SCK);
  scale.set_scale(calibration_factor);
  scale.tare();

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  last_hardware_check = millis();
}

// ------------------- Loop -------------------
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  if (millis() - last_alert_time > 4000) {
    lcd_status = "Secure";
  }

  // =======================================================
  // SOURCE 1: IR SENSOR (STRICTLY DIGITAL TAMPER)
  // =======================================================
  if (IrReceiver.decode()) {
    uint16_t received_cmd = IrReceiver.decodedIRData.command;
    
    if (received_cmd == ir_cmd_add) {
      simulated_tamper_offset += 50.0; 
      Serial.println("📡 IR DETECTED: Digital Injection (+)");
      sendTamperAlert("Digital Hack", smoothed_pure + simulated_tamper_offset);
    }
    else if (received_cmd == ir_cmd_sub) {
      simulated_tamper_offset -= 50.0; 
      Serial.println("📡 IR DETECTED: Digital Injection (-)");
      sendTamperAlert("Digital Hack", smoothed_pure + simulated_tamper_offset);
    }
    else if (received_cmd == ir_cmd_tare) {
      // THE SECRET RESET BUTTON
      Serial.println("📡 IR DETECTED: Taring scale to ZERO...");
      
      lcd.setCursor(0, 1);
      lcd.print("Taring Scale... "); 
      
      scale.tare(); 
      simulated_tamper_offset = 0.0; 
      smoothed_pure = 0.0; 
      prev_pure_weight = 0.0;
      lcd_status = "Secure"; 
      
      delay(500); 
    }
    else {
      Serial.print("📡 Unknown IR Button Pressed. Code: ");
      Serial.println(received_cmd);
    }
    
    IrReceiver.resume(); 
    goto update_displays; 
  }

  // =======================================================
  // SOURCE 2: HX711 SENSOR (STRICTLY PHYSICAL TAMPER)
  // =======================================================
  if (!scale.is_ready()) {
    if (millis() - last_hardware_check > 2000) { 
      sendTamperAlert("HW Disconnect", 0.0);
      last_hardware_check = millis(); 
    }
    goto update_displays; 
  }
  last_hardware_check = millis(); 

  { 
    float pure_rawSample = scale.get_units(1);

    // -------- Physical Check 1: Tray Lifted --------
    if (pure_rawSample < LIFT_THRESHOLD) {
      if (!tray_lifted_state) {
        sendTamperAlert("Tray Lifted", pure_rawSample + simulated_tamper_offset);
        tray_lifted_state = true; 
      }
      smoothed_pure = pure_rawSample; 
      is_moving = false;
      continuous_alert_sent = false;
      prev_pure_weight = smoothed_pure;
      goto update_displays; 
    } else {
      tray_lifted_state = false; 
    }

    // -------- Smart Smoothing for Physical Weight --------
    if (fabs(pure_rawSample - smoothed_pure) > 5.0) {
      smoothed_pure = pure_rawSample; 
    } else {
      smoothed_pure = (smoothed_pure * 0.6f) + (pure_rawSample * 0.4f); 
    }
    
    if (fabs(smoothed_pure) < 0.5f) smoothed_pure = 0.0f; 
    
    // -------- Physical Check 2: Finger Press Logic --------
    float delta = fabs(smoothed_pure - prev_pure_weight);
    if (delta > MOTION_THRESHOLD) {
      last_motion_time = millis(); 
      if (!is_moving) {
        is_moving = true;
        motion_start_time = millis(); 
      }
    }

    if (is_moving) {
      if (millis() - last_motion_time > 1000) {
        is_moving = false;
        continuous_alert_sent = false; 
      }
      else if (millis() - motion_start_time > SETTLE_TIME_MS) {
        if (!continuous_alert_sent) { 
          sendTamperAlert("Finger Press", smoothed_pure + simulated_tamper_offset);
          continuous_alert_sent = true; 
        }
      }
    }
    prev_pure_weight = smoothed_pure;
  }

// =======================================================
// FINAL DISPLAY UPDATE
// =======================================================
update_displays:
  
  float display_weight = smoothed_pure + simulated_tamper_offset;

  Serial.print("Weight: ");
  Serial.print(display_weight, 1);
  Serial.print(" g  |  Status: ");
  Serial.println(lcd_status);

  lcd.setCursor(0, 0);
  lcd.print("Wt: ");
  lcd.print(display_weight, 1);
  lcd.print(" g        "); 

  lcd.setCursor(0, 1);
  if (lcd_status == "Secure") {
    lcd.print("Status: ");
    lcd.print(lcd_status);
  } else {
    lcd.print("! ");
    lcd.print(lcd_status);
  }
  lcd.print("          "); 

  delay(10);
}