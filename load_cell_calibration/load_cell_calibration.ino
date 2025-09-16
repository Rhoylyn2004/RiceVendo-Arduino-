/* ESP32 + HX711 + I2C LCD
   - Press 'c' in Serial Monitor to run calibration using known weight (default 245 g).
   - Calibration factor is saved in Preferences (nonvolatile).
   - After calibration, the LCD shows weight in kg.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"
#include <Preferences.h>

#define DT_PIN 4
#define SCK_PIN 5

LiquidCrystal_I2C lcd(0x27, 16, 2); // change 0x27 if your address differs
HX711 scale;
Preferences prefs;

const char *PREF_NAMESPACE = "loadcell";
const char *KEY_CAL = "cal_factor";
const char *KEY_ZERO = "raw_zero";

float calibration_factor = 0.0; // loaded from prefs or computed
long raw_zero = 0;              // baseline raw value (tare), saved

// Known weight in kilograms (change if you use a different known weight)
const float KNOWN_WEIGHT_KG = 0.245; // 245 g

// helper: read averaged raw value (N samples)
long readRawAverage(int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; ++i) {
    while (!scale.is_ready()) delay(5);
    sum += scale.read(); // raw reading
    delay(20);
  }
  return sum / samples;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  lcd.init();
  lcd.backlight();

  scale.begin(DT_PIN, SCK_PIN);
  // load saved prefs
  prefs.begin(PREF_NAMESPACE, false);
  calibration_factor = prefs.getFloat(KEY_CAL, 0.0f);
  raw_zero = prefs.getLong(KEY_ZERO, 0L);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("HX711 Ready");
  delay(800);

  Serial.println();
  Serial.println("=== HX711 + ESP32 Calibration Utility ===");
  Serial.println("Commands:");
  Serial.println("  c  -> start calibration");
  Serial.println("  t  -> tare (set current raw as zero and save)");
  Serial.println("Any other key -> show raw & weight (if calibrated)");
  Serial.println();

  if (calibration_factor != 0.0) {
    Serial.print("Loaded calibration_factor: ");
    Serial.println(calibration_factor, 6);
    Serial.print("Loaded raw_zero: ");
    Serial.println(raw_zero);
  } else {
    Serial.println("No calibration factor saved yet.");
  }

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Ready");
  delay(400);
}

void loop() {
  // If serial data available, handle commands
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'C') {
      runCalibration();
    } else if (c == 't' || c == 'T') {
      doTareAndSave();
    }
  }

  // Normal operation: display raw and scaled weight (if calibrated)
  long raw = readRawAverage(10);
  Serial.print("Raw: ");
  Serial.print(raw);
  if (calibration_factor != 0.0 && raw_zero != 0) {
    float weight = (raw - raw_zero) / calibration_factor; // kg
    if (weight < 0.0) weight = 0.0; // avoid small negative jitter
    Serial.print("   Weight: ");
    Serial.print(weight, 3);
    Serial.println(" kg");
    // show on LCD
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("W: ");
    lcd.print(weight, 3);
    lcd.print(" kg");
    lcd.setCursor(0,1);
    lcd.print("Raw:");
    lcd.print(raw);
  } else {
    Serial.println();
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Raw:");
    lcd.print(raw);
    lcd.setCursor(0,1);
    lcd.print("Not Calibrated");
  }

  delay(600);
}

void doTareAndSave() {
  Serial.println("Taring now. Make sure no load is on the cell.");
  delay(1000);
  long measured_zero = readRawAverage(15);
  raw_zero = measured_zero;
  prefs.putLong(KEY_ZERO, raw_zero);
  Serial.print("New raw_zero saved: ");
  Serial.println(raw_zero);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Tare saved");
  lcd.setCursor(0,1);
  lcd.print("raw_zero:");
  lcd.print(raw_zero);
  delay(1200);
}

void runCalibration() {
  Serial.println("=== Calibration started ===");
  Serial.println("Step 1: Remove all weight from the load cell. Press ENTER in Serial Monitor when ready.");
  waitForEnter();
  long z = readRawAverage(20);
  Serial.print("Baseline raw (no weight): ");
  Serial.println(z);

  Serial.println("Step 2: Place known weight on load cell (245 g). Press ENTER when stable.");
  waitForEnter();
  long w = readRawAverage(20);
  Serial.print("Raw with known weight: ");
  Serial.println(w);

  long raw_diff = w - z;
  if (raw_diff <= 0) {
    Serial.println("Error: raw difference is zero or negative. Check wiring/orientation and try again.");
    return;
  }

  float new_cal_factor = (float)raw_diff / KNOWN_WEIGHT_KG; // raw per kg
  // save both zero and calibration
  raw_zero = z;
  calibration_factor = new_cal_factor;

  prefs.putLong(KEY_ZERO, raw_zero);
  prefs.putFloat(KEY_CAL, calibration_factor);

  Serial.println();
  Serial.print("Computed calibration_factor (raw per kg): ");
  Serial.println(calibration_factor, 6);
  Serial.print("Saved raw_zero: ");
  Serial.println(raw_zero);
  Serial.println("Calibration saved to Preferences.");
  Serial.println("Test: current weight reading (should be close to 0.245 kg):");

  // test read
  long test_raw = readRawAverage(20);
  float measured_kg = (test_raw - raw_zero) / calibration_factor;
  Serial.print("Measured: ");
  Serial.print(measured_kg, 4);
  Serial.println(" kg");

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Cal saved");
  lcd.setCursor(0,1);
  lcd.print(measured_kg, 3);
  lcd.print(" kg");
  delay(1200);
}

void waitForEnter() {
  // Wait until Serial gets newline or any char
  while (!Serial.available()) {
    delay(50);
  }
  // consume remaining chars
  while (Serial.available()) Serial.read();
}

