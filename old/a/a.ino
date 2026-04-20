#include "WiFi.h"
#include <esp_now.h>
#include <WiFi.h>

const int LEDPIN = 25;
const int BTNPIN = 13;


int buttonState;
int lastButtonState = LOW;
int ledState = LOW;

unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 5;    // the debounce time; increase if the output flickers

void setup(){
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  pinMode(LEDPIN, OUTPUT);
  pinMode(BTNPIN, INPUT);

  buttonState = digitalRead(BTNPIN);

  Serial.println("Setup Complete!");
}

void loop(){
  int reading = digitalRead(BTNPIN);

  if(reading != lastButtonState){
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState){
      buttonState = reading;
      Serial.println(reading);

      if(reading == LOW){
        score();
      } else{
        digitalWrite(LEDPIN, LOW);
        Serial.println("OFF");
      }
      Serial.println();
    }
  }


  lastButtonState = reading;
}

void score(){
  digitalWrite(LEDPIN, HIGH);
  Serial.println("Touch");
  delay(1000);
  // digitalWrite(LEDPIN, LOW);
}

