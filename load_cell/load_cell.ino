#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"

// I2C LCD address (usually 0x27 or 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// HX711 pins
#define DT 4
#define SCK 5
HX711 scale;

void setup() {
  Serial.begin(115200);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  // Initialize Load Cell
  scale.begin(DT, SCK);
  scale.set_scale(420);   // 🔧 Adjust after calibration
  scale.tare();           // Reset to 0

  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Weight: 0.00 kg");
}

void loop() {
  float total = 0.0;
  int samples = 10;

  for (int i = 0; i < samples; i++) {
    total += scale.get_units();
    delay(50);
  }

  float avgWeight = total / samples;

  // Clamp values between 0.00 and 10.00 kg
  if (avgWeight < 0.00) avgWeight = 0.00;
  if (avgWeight > 10.00) avgWeight = 10.00;

  // Print to Serial
  Serial.print("Weight: ");
  Serial.print(avgWeight, 2);
  Serial.println(" kg");

  // Print to LCD
  lcd.setCursor(0, 0);
  lcd.print("Weight:          ");  // Clear line
  lcd.setCursor(8, 0);
  lcd.print(avgWeight, 2);
  lcd.print(" kg");

  delay(500);
}
