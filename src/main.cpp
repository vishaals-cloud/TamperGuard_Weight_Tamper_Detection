#include "HX711.h"

// ------------------- Pin Configuration -------------------
#define DT 32    // HX711 DOUT
#define SCK 33   // HX711 SCK

HX711 scale;

// ------------------- Calibration & Smoothing -------------------
float calibration_factor = 490.56;   // your calibrated value
float smoothed = 0.0;

// ------------------- Sudden jump params -------------------
const float JUMP_PERCENT = 0.2;   // 20% jump threshold
const unsigned long JUMP_TIME_MS = 200; // time window to consider jump (ms)

// ------------------- State -------------------
float prev_weight = 0.0;
unsigned long prev_time = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TamperGuard (Zero Manipulation + Sudden Jump)");
  delay(200);

  scale.begin(DT, SCK);
  scale.set_scale(calibration_factor);
  scale.tare();                      // zero at startup
  Serial.println("Tare complete. Ready.");
  delay(500);

  // initialize prev_time
  prev_time = millis();
  prev_weight = 0.0;
}

void loop() {
  if (!scale.is_ready()) {
    Serial.println("⚠️ HX711 not responding. Check wiring!");
    delay(500);
    return;
  }

  // --- Read & smooth (fast single-sample + 80/20 smoothing) ---
  float rawSample = scale.get_units(1);          // one quick sample
  smoothed = (smoothed * 0.8f) + (rawSample * 0.2f);

  // --- small-noise threshold (adjustable) ---
  if (fabs(smoothed) < 2.0f) smoothed = 0.0f;

  float current_weight = smoothed;
  unsigned long current_time = millis();
  unsigned long dt = current_time - prev_time;

  // ---------- Rule A: Zero Manipulation ----------
  // If displayed weight is zero but raw ADC is still showing activity,
  // it may indicate forced-zero or tampering.
  long rawADC = scale.read(); // raw 24-bit value from HX711
  if (current_weight == 0.0f && llabs(rawADC) > 100) {
    Serial.print("⚠️ ALERT: Zero Manipulation Suspected");
    Serial.print(" | RawADC: ");
    Serial.print(rawADC);
    Serial.print(" | Weight: ");
    Serial.print(current_weight, 2);
    Serial.println(" g");
  } else {
    // ---------- Rule B: Sudden Jump ----------
    float delta = fabs(current_weight - prev_weight);
    // Compare delta to a percentage of previous weight (use 1.0 if prev is tiny)
    float compareBase = max(1.0f, fabs(prev_weight));
    if (dt > 0 && (delta > (JUMP_PERCENT * compareBase)) && dt < JUMP_TIME_MS) {
      Serial.print("⚠️ ALERT: Sudden Jump Detected");
      Serial.print(" | Δ: ");
      Serial.print(delta, 2);
      Serial.print(" g");
      Serial.print(" | dt: ");
      Serial.print(dt);
      Serial.print(" ms");
      Serial.print(" | Weight: ");
      Serial.print(current_weight, 2);
      Serial.println(" g");
    } else {
      // Normal output
      Serial.print("Weight: ");
      Serial.print(current_weight, 2);
      Serial.println(" g  -> Status: OK");
    }
  }

  // --- Update previous state ---
  prev_weight = current_weight;
  prev_time = current_time;

  delay(100); // 10 updates/sec (matches smoothing choice)
}
