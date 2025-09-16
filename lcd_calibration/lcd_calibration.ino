#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ✅ LCD setup (2004A I2C)
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Try 0x3F if 0x27 doesn’t work

// WiFi credentials
const char* ssid = "Realme c67";
const char* password = "123456789";

// ✅ Firebase URLs
const char* urlUltrasonic = "https://ricevendo-4e1fe-default-rtdb.asia-southeast1.firebasedatabase.app/ultrasonic.json";
const char* urlStocks     = "https://ricevendo-4e1fe-default-rtdb.asia-southeast1.firebasedatabase.app/stocks.json";

// ✅ Track last known values
String riceA = "Unknown";
String riceB = "Unknown";
int priceA = 0;
int priceB = 0;
int lastPercentA = -1;
int lastPercentB = -1;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ✅ LCD init
  lcd.init();
  lcd.backlight();

  // ✅ Splash screen
  lcd.clear();
  lcd.setCursor(4, 0); lcd.print("RiceVendo");
  lcd.setCursor(2, 1); lcd.print("IoT Rice Vending");
  lcd.setCursor(3, 2); lcd.print("Machine Ready...");
  delay(3000);

  // ✅ Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected to WiFi.");
  lcd.clear();
  lcd.setCursor(3, 1); lcd.print("WiFi Connected!");
  delay(2000);
}

void fetchFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  // ✅ Fetch ultrasonic
  http.begin(urlUltrasonic);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("📥 Ultrasonic: " + payload);
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      lastPercentA = doc["containerA"] | -1;
      lastPercentB = doc["containerB"] | -1;
    }
  }
  http.end();

  // ✅ Fetch stocks
  http.begin(urlStocks);
  httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("📥 Stocks: " + payload);
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      riceA = String(doc["containerA"]["classification"] | "Unknown");
      riceB = String(doc["containerB"]["classification"] | "Unknown");
      priceA = doc["containerA"]["price"] | 0;
      priceB = doc["containerB"]["price"] | 0;
    }
  }
  http.end();
}

// ✅ Utility: Truncate long words for LCD
String fitToLCD(String text, int maxLen) {
  if (text.length() > maxLen) {
    return text.substring(0, maxLen - 1) + ".";  // cut + add dot
  }
  return text;
}

// ✅ Display formatted info
void displayContainer(String name, int price, int stockPercent, char label) {
  lcd.clear();

  // Line 1 → Title centered
  lcd.setCursor(5, 0);
  lcd.print("RiceVendo");

  // Line 2 → Container + Name (fit to 20 chars)
  lcd.setCursor(0, 1);
  String row2 = "Cont. ";
  row2 += label;
  row2 += ": " + name;
  lcd.print(fitToLCD(row2, 20));

  // Line 3 → Price
  lcd.setCursor(0, 2);
  lcd.print("Price ");
  lcd.print(price);
  lcd.print(" PHP/kg");

  // Line 4 → Stock
  lcd.setCursor(0, 3);
  lcd.print("Stock ");
  if (stockPercent >= 0) lcd.print(stockPercent);
  else lcd.print("--");
  lcd.print("%");
}

void loop() {
  // ✅ Update from Firebase
  fetchFirebase();

  // ✅ Show Container A
  displayContainer(riceA, priceA, lastPercentA, 'A');
  delay(4000);

  // ✅ Show Container B
  displayContainer(riceB, priceB, lastPercentB, 'B');
  delay(4000);
}
