#include <WiFi.h>
#include <esp_now.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <Preferences.h>

// ----------------- Servos -----------------
Servo servoA, servoB;
const int servoPinA = 18;
const int servoPinB = 19;
const int closedAngle = 0;
const int openAngle   = 90;

// ----------------- HX711 -----------------
#define DT_PIN_A 4
#define SCK_PIN_A 5
HX711 scaleA;

#define DT_PIN_B 16
#define SCK_PIN_B 17
HX711 scaleB;

// ----------------- Preferences -----------------
Preferences prefs;
const char *PREF_NAMESPACE_A = "loadcellA";
const char *PREF_NAMESPACE_B = "loadcellB";
const char *KEY_CAL = "cal_factor";
const char *KEY_ZERO = "raw_zero";

float calA = 0.0, calB = 0.0;
long zeroA = 0, zeroB = 0;

// ----------------- State -----------------
bool dispensingA = false;
bool dispensingB = false;
float targetA_g = 0.0;
float targetB_g = 0.0;

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 300;

uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// Offline triggers (e.g. physical button pins)
const int btnA = 32;
const int btnB = 33;

// ----------------- Helpers -----------------
void sendMsg(const char *msg) {
  esp_err_t res = esp_now_send(broadcastAddress, (uint8_t*)msg, strlen(msg)+1);
  Serial.printf("Sent: %s (res=%d)\n", msg, (int)res);
}

float readWeightGrams(HX711 &scale, float cal, long zero, int samples=6) {
  long sum=0; int got=0;
  for(int i=0;i<samples;i++) {
    if (!scale.is_ready()) { delay(5); continue; }
    sum += scale.read(); got++; delay(10);
  }
  if (got==0) return 0.0;
  long avg=sum/got;
  float kg=(avg-zero)/cal;
  float g=kg*1000.0;
  return (g<0.0)?0.0:g;
}

// ----------------- ESP-NOW -----------------
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  String msg;
  for(int i=0;i<len;i++) msg+=(char)data[i];
  Serial.println("RX: "+msg);

  if (msg.startsWith("DISPENSE_A:")) {
    targetA_g=msg.substring(11).toFloat();
    scaleA.tare(); delay(100);
    servoA.write(openAngle);
    dispensingA=true;
  } else if (msg.startsWith("DISPENSE_B:")) {
    targetB_g=msg.substring(11).toFloat();
    scaleB.tare(); delay(100);
    servoB.write(openAngle);
    dispensingB=true;
  }
}

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);

  // Servos
  servoA.attach(servoPinA);
  servoB.attach(servoPinB);
  servoA.write(closedAngle);
  servoB.write(closedAngle);

  // HX711
  scaleA.begin(DT_PIN_A,SCK_PIN_A);
  scaleB.begin(DT_PIN_B,SCK_PIN_B);

  // Buttons
  pinMode(btnA, INPUT_PULLUP);
  pinMode(btnB, INPUT_PULLUP);

  // Load saved calibration
  prefs.begin(PREF_NAMESPACE_A, true);
  calA = prefs.getFloat(KEY_CAL, 1000.0f);
  zeroA = prefs.getLong(KEY_ZERO, 0L);
  prefs.end();

  prefs.begin(PREF_NAMESPACE_B, true);
  calB = prefs.getFloat(KEY_CAL, 1000.0f);
  zeroB = prefs.getLong(KEY_ZERO, 0L);
  prefs.end();

  Serial.printf("Cal A=%.2f ZeroA=%ld | Cal B=%.2f ZeroB=%ld\n",calA,zeroA,calB,zeroB);

  // ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init()!=ESP_OK) {
    Serial.println("⚠ ESP-NOW not initialized — OFFLINE mode only");
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_peer_info_t peerInfo={};
    memcpy(peerInfo.peer_addr,broadcastAddress,6);
    peerInfo.channel=0; peerInfo.encrypt=false;
    esp_now_add_peer(&peerInfo);
  }
}

// ----------------- Loop -----------------
void loop() {
  unsigned long now = millis();

  // ----------------- OFFLINE MODE -----------------
  if (!dispensingA && digitalRead(btnA)==LOW) {
    Serial.println("OFFLINE DISPENSE A TRIGGERED");
    targetA_g = 100.0; // default 100g if offline
    scaleA.tare();
    servoA.write(openAngle);
    dispensingA = true;
    delay(300); // debounce
  }
  if (!dispensingB && digitalRead(btnB)==LOW) {
    Serial.println("OFFLINE DISPENSE B TRIGGERED");
    targetB_g = 100.0; // default 100g if offline
    scaleB.tare();
    servoB.write(openAngle);
    dispensingB = true;
    delay(300); // debounce
  }

  // ----------------- Dispensing A -----------------
  if (dispensingA) {
    float g=readWeightGrams(scaleA,calA,zeroA);
    Serial.printf("Weight A: %.1fg / Target %.1fg\n", g, targetA_g);

    if (now-lastSend>=SEND_INTERVAL_MS) {
      lastSend=now;
      char buf[32];
      snprintf(buf,sizeof(buf),"WEIGHT_A:%.1f",g);
      sendMsg(buf);
    }

    if (g>=targetA_g) {
      servoA.write(closedAngle);
      dispensingA=false;
      sendMsg("DONE_A");
    }
  }

  // ----------------- Dispensing B -----------------
  if (dispensingB) {
    float g=readWeightGrams(scaleB,calB,zeroB);
    Serial.printf("Weight B: %.1fg / Target %.1fg\n", g, targetB_g);

    if (now-lastSend>=SEND_INTERVAL_MS) {
      lastSend=now;
      char buf[32];
      snprintf(buf,sizeof(buf),"WEIGHT_B:%.1f",g);
      sendMsg(buf);
    }

    if (g>=targetB_g) {
      servoB.write(closedAngle);
      dispensingB=false;
      sendMsg("DONE_B");
    }
  }
}
