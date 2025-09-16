#include <ESP32Servo.h>

Servo myservo;
int pos = 0;

void setup() {
  myservo.attach(2);
}

void loop() {
  for(pos = 0; pos <= 180; pos += 5) {
    myservo.write(pos);
    delay(15);
  }
  
  for(pos = 180; pos >= 0; pos -= 5) {  // Fixed condition
    myservo.write(pos);
    delay(15);
  }
}

