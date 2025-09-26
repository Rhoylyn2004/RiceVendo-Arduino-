#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_now.h>
#include <Preferences.h>

// ✅ LCD setup (2004A I2C)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ✅ ESP-NOW peer (ESP2 MAC – replace if different)
uint8_t peerAddress[] = {0x44, 0x1D, 0x64, 0xF2, 0xFB, 0xB0};

// WiFi credentials
const char* ssid = "Realme c67";
const char* password = "123456789";

// ✅ Firebase URL
const char* urlUltrasonic = "https://ricevendo-4e1fe-default-rtdb.asia-southeast1.firebasedatabase.app/ultrasonic.json";
const char* urlStocks     = "https://ricevendo-4e1fe-default-rtdb.asia-southeast1.firebasedatabase.app/stocks.json";

// ✅ Cached values
String riceA = "Local A";
String riceB = "Local B";
int priceA = 50;
int priceB = 55;
int lastPercentA = -1;
int lastPercentB = -1;

// Preferences for offline storage
Preferences prefs;

// ✅ Button pins
#define BTN_A       18
#define BTN_B       19
#define BTN_CANCEL  23
#define BTN_CONFIRM 25

// ✅ Coin acceptor pin
#define COIN_PIN 4
volatile int pulseCount = 0;
unsigned long lastPulseTime = 0;
const unsigned long coinTimeout = 300;
const unsigned long minPulseGap = 20;
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

// ✅ Target grams
int targetGrams = 0;

// ✅ Weight update control
String lastWeight = "--";
float lastWeightValue = -1;  // for stable updates
unsigned long lastWeightUpdate = 0;
const unsigned long WEIGHT_DISPLAY_INTERVAL_MS = 500;

// ======================== PROTOTYPES ========================
void fetchFirebase();
void saveCache();
void loadCache();
void showHomeSlide();
void showPicked();
void showConfirmDetails();
void showDispensingStart();
void showDispensingDone();
bool isPressed(int pin);
void sendCommand(const char *cmd);

// ======================== ESP-NOW callbacks ========================
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  String msg;
  for (int i = 0; i < len; i++) msg += (char)data[i];
  Serial.println("📩 Received: " + msg);

  // Weight updates: ONLY show while dispensingInProgress and only for selected container
  if ((msg.startsWith("WEIGHT_A:") || msg.startsWith("WEIGHT_B:"))) {
    // parse grams
    String gramsStr = msg.substring(msg.indexOf(":") + 1);
    float gVal = gramsStr.toFloat();

    // determine if this weight should be shown now:
    bool isA = msg.startsWith("WEIGHT_A:");
    bool showNow = dispensingInProgress && (
                    (isA && selectedContainer == 'A') ||
                    (!isA && selectedContainer == 'B')
                   );

    // Stabilization: ignore tiny 0.0 jitter; throttle updates
    if (showNow && gVal > 0.05 &&
        (millis() - lastWeightUpdate > WEIGHT_DISPLAY_INTERVAL_MS) &&
        (fabs(gVal - lastWeightValue) > 0.5 || lastWeightValue < 0.0)) {

      lastWeight = gramsStr;
      lastWeightValue = gVal;
      lcd.setCursor(0, 2);
      // pad to clear previous content
      lcd.print("Weight: " + gramsStr + " g   ");
      lastWeightUpdate = millis();
    }
    // else ignore weight update to avoid home screen flicker
    return;
  }

  // DONE messages end the dispensing session.
  if (msg == "DONE_A" || msg == "DONE_B") {
    lcd.setCursor(5, 3);
    lcd.print("Done!          ");
    dispensingInProgress = false;
    totalBalance = 0;
    Serial.println("Received DONE, returning HOME");
    delay(1200);
    currentState = HOME;
  }
}

// send simple null-terminated command via ESP-NOW to peerAddress
void sendCommand(const char *cmd) {
  esp_err_t result = esp_now_send(peerAddress, (uint8_t *)cmd, strlen(cmd) + 1);
  if (result == ESP_OK) {
    Serial.printf("✅ Sent: %s\n", cmd);
  } else {
    Serial.printf("⚠️ Failed to send: %s (err=%d)\n", cmd, (int)result);
    // if peer send fails, attempt to re-add peer once (debug)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = WiFi.channel(); // try correct channel
    peerInfo.encrypt = false;
    esp_err_t r2 = esp_now_add_peer(&peerInfo);
    Serial.printf("esp_now_add_peer retry result=%d\n", (int)r2);
  }
}

// ======================== COIN ISR ========================
void IRAM_ATTR coinISR() {
  unsigned long now = millis();
  if (now - lastPulseTime > minPulseGap) {
    pulseCount++;
    lastPulseTime = now;
  }
}

// ======================== CACHE ========================
void saveCache() {
  prefs.begin("ricevendo", false);
  prefs.putString("riceA", riceA);
  prefs.putString("riceB", riceB);
  prefs.putInt("priceA", priceA);
  prefs.putInt("priceB", priceB);
  prefs.putInt("lastPercentA", lastPercentA);
  prefs.putInt("lastPercentB", lastPercentB);
  prefs.end();
  Serial.println("💾 Cached values saved.");
}

void loadCache() {
  prefs.begin("ricevendo", true);
  riceA = prefs.getString("riceA", riceA);
  riceB = prefs.getString("riceB", riceB);
  priceA = prefs.getInt("priceA", priceA);
  priceB = prefs.getInt("priceB", priceB);
  lastPercentA = prefs.getInt("lastPercentA", lastPercentA);
  lastPercentB = prefs.getInt("lastPercentB", lastPercentB);
  prefs.end();
  Serial.println("📂 Loaded cached values.");
}

// ======================== SETUP ========================
void setup() {
  Serial.begin(115200);
  delay(500);

  lcd.init();
  lcd.backlight();

  // Buttons
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_CANCEL, INPUT_PULLUP);
  pinMode(BTN_CONFIRM, INPUT_PULLUP);
  pinMode(COIN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(COIN_PIN), coinISR, FALLING);

  // --- Boot splash (static) ---
  lcd.clear();
  lcd.setCursor(4, 0); lcd.print("RiceVendo");
  lcd.setCursor(2, 1); lcd.print("IoT Rice Vending");
  lcd.setCursor(3, 2); lcd.print("Machine Ready");

  // --- Dot animation (3 cycles) ---
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int dots = 0; dots < 4; dots++) {
      lcd.setCursor(16, 2); // right side of line
      if (dots == 0) lcd.print("   ");
      else if (dots == 1) lcd.print(".  ");
      else if (dots == 2) lcd.print(".. ");
      else if (dots == 3) lcd.print("...");
      delay(400);
    }
  }

  // --- Initialize ESP-NOW (always attempt) ---
  WiFi.mode(WIFI_STA);
  Serial.print("ESP1 MAC: "); Serial.println(WiFi.macAddress());
  esp_err_t en = esp_now_init();
  if (en != ESP_OK) {
    Serial.printf("⚠️ esp_now_init failed (err=%d)\n", (int)en);
  } else {
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    // register peer & set channel to WiFi.channel() for better reliability
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = WiFi.channel(); // use current AP channel (if any)
    peerInfo.encrypt = false;
    esp_err_t r = esp_now_add_peer(&peerInfo);
    Serial.printf("esp_now_add_peer result=%d\n", (int)r);
    // If failed, attempt a second time with channel 0 (legacy fallback)
    if (r != ESP_OK) {
      peerInfo.channel = 0;
      r = esp_now_add_peer(&peerInfo);
      Serial.printf("esp_now_add_peer retry channel0 result=%d\n", (int)r);
    }
  }

  // --- Try WiFi connect with a slightly longer timeout and animated dots ---
  WiFi.begin(ssid, password);
  unsigned long startAttempt = millis();
  unsigned long connectTimeout = 10000UL; // 10 seconds
  int dotState = 0;
  lcd.setCursor(3, 2); lcd.print("Connecting WiFi");
  while (millis() - startAttempt < connectTimeout) {
    if (WiFi.status() == WL_CONNECTED) break;
    // animate dot at right
    lcd.setCursor(18, 2);
    if (dotState == 0) lcd.print(" ");
    else if (dotState == 1) lcd.print(".");
    else if (dotState == 2) lcd.print("..");
    else lcd.print("...");
    dotState = (dotState + 1) % 4;
    delay(400);
    Serial.print(".");
  }

  // --- Mode result ---
  lcd.clear();
  lcd.setCursor(4, 0); lcd.print("RiceVendo");
  lcd.setCursor(2, 1); lcd.print("IoT Rice Vending");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi OK");
    lcd.setCursor(3, 2); lcd.print("Online Mode");
    // fetch firebase (guarded inside)
    fetchFirebase();
  } else {
    Serial.println("\n⚠️ Offline mode");
    lcd.setCursor(3, 2); lcd.print("Offline Mode");
    loadCache();
  }

  delay(1200);
  showHomeSlide();
}

// ======================== LOOP ========================
unsigned long lastFetch = 0;
void loop() {
  // Process coin insertion (debounced pulse train)
  if (pulseCount > 0 && (millis() - lastPulseTime > coinTimeout)) {
    coinInserted = pulseCount;
    pulseCount = 0;
    totalBalance += coinInserted;
    Serial.printf("Coin Inserted: %d PHP | Total: %d PHP\n", coinInserted, totalBalance);

    if (currentState == CONFIRM_DETAILS) showConfirmDetails();
  }

  // Fetch Firebase only if connected
  if (WiFi.status() == WL_CONNECTED && millis() - lastFetch > 10000) {
    fetchFirebase();
    lastFetch = millis();
  }

  // Home slideshow
  if (currentState == HOME && millis() - lastSlide > 3000) {
    showA = !showA;
    showHomeSlide();
    lastSlide = millis();
  }

  // Update LCD when state changes
  if (currentState != lastState && currentState != HOME) {
    switch (currentState) {
      case PICKED: showPicked(); break;
      case CONFIRM_DETAILS: showConfirmDetails(); break;
      case DISPENSING: showDispensingStart(); break;
      default: break;
    }
    lastState = currentState;
  }

  // Button handling (non-blocking)
  switch (currentState) {
    case HOME:
      if (isPressed(BTN_A)) { selectedContainer='A'; currentState=PICKED; }
      else if (isPressed(BTN_B)) { selectedContainer='B'; currentState=PICKED; }
      break;

    case PICKED:
      if (isPressed(BTN_CONFIRM)) currentState=CONFIRM_DETAILS;
      if (isPressed(BTN_CANCEL)) currentState=HOME;
      break;

    case CONFIRM_DETAILS:
      if (isPressed(BTN_CONFIRM)) {
        if (totalBalance <= 0) {
          // Flash "Insert coins first"
          lcd.clear();
          lcd.setCursor(2,1); lcd.print("Insert coins first");
          delay(900);
          showConfirmDetails();
        } else {
          currentState = DISPENSING;
        }
      }
      if (isPressed(BTN_CANCEL)) currentState=HOME;
      break;

    case DISPENSING:
      // waiting for DONE message from ESP2 (which will switch state back to HOME)
      break;
  }

  delay(5);
}

// ======================== FIREBASE ========================
void fetchFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  http.begin(urlUltrasonic);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("Ultrasonic JSON parse failed: ");
      Serial.println(err.c_str());
    } else {
      lastPercentA = doc["containerA"] | lastPercentA;
      lastPercentB = doc["containerB"] | lastPercentB;
    }
  }
  http.end();

  http.begin(urlStocks);
  httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("Stocks JSON parse failed: ");
      Serial.println(err.c_str());
    } else {
      riceA = String(doc["containerA"]["classification"] | riceA);
      riceB = String(doc["containerB"]["classification"] | riceB);
      priceA = doc["containerA"]["price"] | priceA;
      priceB = doc["containerB"]["price"] | priceB;
    }
  }
  http.end();
  saveCache();
}

// ======================== DISPLAY ========================
String fitToLCD(String text, int maxLen) {
  return (text.length() > maxLen) ? text.substring(0, maxLen - 1) + "." : text;
}
void showHomeSlide() {
  lcd.clear(); lcd.setCursor(5, 0); lcd.print("RiceVendo");
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
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("You Picked Rice "); lcd.print(selectedContainer);
  lcd.setCursor(0, 1); lcd.print("Start dispensing?");
  lcd.setCursor(0, 2); lcd.print("[Confirm]");
  lcd.setCursor(0, 3); lcd.print("[Cancel]");
}
void showConfirmDetails() {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Container "); lcd.print(selectedContainer);
  if (selectedContainer == 'A') {
    lcd.setCursor(0, 1); lcd.print("Name: " + fitToLCD(riceA, 14));
    lcd.setCursor(0, 2); lcd.print("Price: " + String(priceA) + " PHP/kg");
  } else {
    lcd.setCursor(0, 1); lcd.print("Name: " + fitToLCD(riceB, 14));
    lcd.setCursor(0, 2); lcd.print("Price: " + String(priceB) + " PHP/kg");
  }
  int grams = (selectedContainer == 'A' && priceA > 0) ? (totalBalance*1000)/priceA :
              (selectedContainer == 'B' && priceB > 0) ? (totalBalance*1000)/priceB : 0;
  targetGrams = grams;
  lcd.setCursor(0, 3); lcd.print("Coins:" + String(totalBalance) + " Rice:" + String(grams) + "g");
}
void showDispensingStart() {
  if (totalBalance <= 0) { lcd.clear(); lcd.setCursor(2,1); lcd.print("Insert coins"); delay(800); showConfirmDetails(); currentState = CONFIRM_DETAILS; return; }
  lcd.clear(); lcd.setCursor(2, 1); lcd.print("Dispensing...");
  lcd.setCursor(0, 2); lcd.print("Target: " + String(targetGrams) + " g");
  char buf[32]; if (selectedContainer == 'A') snprintf(buf,sizeof(buf),"DISPENSE_A:%d",targetGrams);
  else snprintf(buf,sizeof(buf),"DISPENSE_B:%d",targetGrams);
  sendCommand(buf); dispensingStart = millis(); dispensingInProgress = true;
}
void showDispensingDone() { lcd.setCursor(5, 2); lcd.print("Done!"); totalBalance = 0; }

// ======================== HELPER ========================
bool isPressed(int pin) {
  static unsigned long lastPress[40] = {0};
  if (digitalRead(pin) == HIGH) {
    if (millis() - lastPress[pin] > 200) {
      lastPress[pin] = millis();
      while(digitalRead(pin) == HIGH);
      return true;
    }
  }
  return false;
}
