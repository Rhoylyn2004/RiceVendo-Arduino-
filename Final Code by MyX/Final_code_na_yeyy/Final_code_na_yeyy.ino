#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ✅ LCD setup (2004A I2C)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// WiFi credentials
const char* ssid = "Realme c67";
const char* password = "123456789";

// ✅ Firebase URLs
const char* urlUltrasonic = "https://ricevendo-4e1fe-default-rtdb.asia-southeast1.firebasedatabase.app/ultrasonic.json";
const char* urlStocks     = "https://ricevendo-4e1fe-default-rtdb.asia-southeast1.firebasedatabase.app/stocks.json";

// ✅ Cached values
String riceA = "Local A";
String riceB = "Local B";
int priceA = 50;   // PHP/kg
int priceB = 55;   // PHP/kg
int lastPercentA = -1;
int lastPercentB = -1;

// ✅ Button pins
#define BTN_A       18
#define BTN_B       19
#define BTN_CANCEL  23
#define BTN_CONFIRM 25

// ✅ Coin acceptor pin
#define COIN_PIN 4
volatile int pulseCount = 0;
unsigned long lastPulseTime = 0;
const unsigned long coinTimeout = 300;  // ms to detect end of pulse train
const unsigned long minPulseGap = 20;   // debounce
int coinInserted = 0;
int totalBalance = 0;

// ✅ State machine
enum State { HOME, PICKED, CONFIRM_DETAILS, DISPENSING };
State currentState = HOME;
State lastState = DISPENSING;
char selectedContainer = ' ';

// ✅ Non-blocking timers
unsigned long dispensingStart = 0;
bool dispensingInProgress = false;

// ✅ Slideshow timer
unsigned long lastSlide = 0;
bool showA = true;

// ======================== COIN ISR ========================
void IRAM_ATTR coinISR() {
  unsigned long now = millis();
  if (now - lastPulseTime > minPulseGap) {
    pulseCount++;
    lastPulseTime = now;
  }
}

// ======================== SETUP ========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  lcd.init();
  lcd.backlight();

  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_CANCEL, INPUT_PULLUP);
  pinMode(BTN_CONFIRM, INPUT_PULLUP);

  pinMode(COIN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(COIN_PIN), coinISR, FALLING);

  lcd.clear();
  lcd.setCursor(4, 0); lcd.print("RiceVendo");
  lcd.setCursor(2, 1); lcd.print("IoT Rice Vending");
  lcd.setCursor(3, 2); lcd.print("Machine Ready...");
  delay(3000);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi.");
    lcd.clear();
    lcd.setCursor(3, 1); lcd.print("WiFi Connected!");
    delay(1500);
  } else {
    Serial.println("\n⚠️ No WiFi. Running offline mode.");
    lcd.clear();
    lcd.setCursor(1, 1); lcd.print("Offline Mode Active");
    delay(2000);
  }
}

// ======================== FIREBASE ========================
void fetchFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  // Ultrasonic
  http.begin(urlUltrasonic);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, payload)) {
      lastPercentA = doc["containerA"] | lastPercentA;
      lastPercentB = doc["containerB"] | lastPercentB;
    }
  }
  http.end();

  // Stocks
  http.begin(urlStocks);
  httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, payload)) {
      riceA = String(doc["containerA"]["classification"] | riceA);
      riceB = String(doc["containerB"]["classification"] | riceB);
      priceA = doc["containerA"]["price"] | priceA;
      priceB = doc["containerB"]["price"] | priceB;
    }
  }
  http.end();
}

// ✅ Utility
String fitToLCD(String text, int maxLen) {
  if (text.length() > maxLen) return text.substring(0, maxLen - 1) + ".";
  return text;
}

// ======================== DISPLAY SCREENS ========================
void showHomeSlide() {
  lcd.clear();
  lcd.setCursor(5, 0); lcd.print("RiceVendo");

  if (showA) {
    lcd.setCursor(0, 1); lcd.print("Rice A: " + fitToLCD(riceA, 14));
    lcd.setCursor(0, 2); lcd.print("Stock: " + (lastPercentA>=0?String(lastPercentA)+"%":"--"));
    lcd.setCursor(0, 3); lcd.print("Price: " + String(priceA) + " PHP");
  } else {
    lcd.setCursor(0, 1); lcd.print("Rice B: " + fitToLCD(riceB, 14));
    lcd.setCursor(0, 2); lcd.print("Stock: " + (lastPercentB>=0?String(lastPercentB)+"%":"--"));
    lcd.setCursor(0, 3); lcd.print("Price: " + String(priceB) + " PHP");
  }
}

void showPicked() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("You Picked Rice "); lcd.print(selectedContainer);
  lcd.setCursor(0, 1); lcd.print("Start dispensing?");
  lcd.setCursor(0, 2); lcd.print("[Green] Confirm");
  lcd.setCursor(0, 3); lcd.print("[Red] Cancel");
}

void showConfirmDetails() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Container "); lcd.print(selectedContainer);
  if (selectedContainer == 'A') {
    lcd.setCursor(0, 1); lcd.print("Name: " + fitToLCD(riceA, 14));
    lcd.setCursor(0, 2); lcd.print("Price: " + String(priceA) + " PHP/kg");
  } else {
    lcd.setCursor(0, 1); lcd.print("Name: " + fitToLCD(riceB, 14));
    lcd.setCursor(0, 2); lcd.print("Price: " + String(priceB) + " PHP/kg");
  }
  // Show coins & rice grams
  int grams = 0;
  if (selectedContainer == 'A' && priceA > 0) grams = (totalBalance * 1000) / priceA;
  if (selectedContainer == 'B' && priceB > 0) grams = (totalBalance * 1000) / priceB;

  lcd.setCursor(0, 3); lcd.print("Coins:" + String(totalBalance) + " Rice:" + String(grams) + "g");
}

void showDispensingStart() {
  lcd.clear();
  lcd.setCursor(2, 1); lcd.print("Dispensing Rice...");
  dispensingStart = millis();
  dispensingInProgress = true;
}

void showDispensingDone() {
  lcd.setCursor(5, 2); lcd.print("Done!");
  totalBalance = 0;  // reset after dispense
}

// ======================== HELPER ========================
bool isPressed(int pin) {
  static unsigned long lastPress[32] = {0};
  if (digitalRead(pin) == HIGH) {
    if (millis() - lastPress[pin] > 200) {
      lastPress[pin] = millis();
      while(digitalRead(pin) == HIGH);
      return true;
    }
  }
  return false;
}

// ======================== LOOP ========================
unsigned long lastFetch = 0;

void loop() {
  // ✅ Process coin insertion
  if (pulseCount > 0 && (millis() - lastPulseTime > coinTimeout)) {
    coinInserted = pulseCount;   // peso value
    pulseCount = 0;
    totalBalance += coinInserted;
    Serial.printf("Coin Inserted: %d PHP | Total: %d PHP\n", coinInserted, totalBalance);

    if (currentState == CONFIRM_DETAILS) {
      showConfirmDetails(); // refresh LCD with updated balance
    }
  }

  // ✅ Fetch Firebase every 10 sec
  if (millis() - lastFetch > 10000) {
    fetchFirebase();
    lastFetch = millis();
  }

  // ✅ Home screen slideshow
  if (currentState == HOME && millis() - lastSlide > 3000) {
    showA = !showA;
    showHomeSlide();
    lastSlide = millis();
  }

  // ✅ LCD update on state change
  if (currentState != lastState && currentState != HOME) {
    switch(currentState) {
      case PICKED: showPicked(); break;
      case CONFIRM_DETAILS: showConfirmDetails(); break;
      case DISPENSING: showDispensingStart(); break;
      default: break;
    }
    lastState = currentState;
  }

  // ✅ Non-blocking dispensing
  if (currentState == DISPENSING && dispensingInProgress) {
    if (millis() - dispensingStart > 3000) {
      showDispensingDone();
      if (millis() - dispensingStart > 5000) {
        currentState = HOME;
        dispensingInProgress = false;
        lastState = DISPENSING;
      }
    }
  }

  // ✅ Button handling
  switch(currentState) {
    case HOME:
      if (isPressed(BTN_A)) { selectedContainer='A'; currentState=PICKED; }
      else if (isPressed(BTN_B)) { selectedContainer='B'; currentState=PICKED; }
      break;
    case PICKED:
      if (isPressed(BTN_CONFIRM)) currentState=CONFIRM_DETAILS;
      if (isPressed(BTN_CANCEL)) currentState=HOME;
      break;
    case CONFIRM_DETAILS:
      if (isPressed(BTN_CONFIRM)) currentState=DISPENSING;
      break;
    case DISPENSING:
      break;
  }
}
