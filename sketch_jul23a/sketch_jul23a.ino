#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Firebase_ESP_Client.h>

// Replace with your network credentials
#define WIFI_SSID "Realme c67"
#define WIFI_PASSWORD "123456789"

// Replace with your Firebase credentials
#define API_KEY "AIzaSyC3Jj8d_Ooywzwbdh76VCGNGLNcfNqxw8E"
#define DATABASE_URL "https://ricevendo-4e1fe-default-rtdb.asia-southeast1.firebasedatabase.app"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// LCD object - I2C address 0x27, 16 cols, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Variables to store values
String classA = "", classB = "";
int priceA = 0, priceB = 0;
int levelA = 0, levelB = 0;

unsigned long lastFetch = 0;
const unsigned long interval = 2000; // Fetch every 2 seconds

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");

  // Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "";  // Anonymous login
  auth.user.password = "";

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  delay(2000);
  lcd.clear();
}

void loop() {
  if (millis() - lastFetch >= interval) {
    lastFetch = millis();
    if (Firebase.ready()) {
      // Stocks
      Firebase.RTDB.getString(&fbdo, "/stocks/containerA/classification");
      classA = fbdo.stringData();

      Firebase.RTDB.getInt(&fbdo, "/stocks/containerA/price");
      priceA = fbdo.intData();

      Firebase.RTDB.getString(&fbdo, "/stocks/containerB/classification");
      classB = fbdo.stringData();

      Firebase.RTDB.getInt(&fbdo, "/stocks/containerB/price");
      priceB = fbdo.intData();

      // Ultrasonic levels
      Firebase.RTDB.getInt(&fbdo, "/ultrasonic/containerA");
      levelA = fbdo.intData();

      Firebase.RTDB.getInt(&fbdo, "/ultrasonic/containerB");
      levelB = fbdo.intData();

      // Display
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("A:");
      lcd.print(classA);
      lcd.print(" ");
      lcd.print(priceA);

      lcd.setCursor(0, 1);
      lcd.print("Lvl:");
      lcd.print(levelA);
      lcd.print("% B:");
      lcd.print(levelB);
      lcd.print("%");
    }
  }
}
