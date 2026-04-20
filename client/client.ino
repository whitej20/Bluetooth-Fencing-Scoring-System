// client.ino
#include "WiFi.h"
#include <WiFi.h>
#include <cmath>
#include <esp_now.h>
#include <esp_wifi.h>

// const int LEDPIN = 25;
const int BTNPIN = 18;
const int TCHPIN = 4;
int touch;
int currTouch;

int ledState = LOW;      // the current state of the output pin
int currState;           // the current reading from the input pin
int prevState = LOW;     // the previous reading from the input pin
float last_received = 0; // time the last touch was received
float last_given = 0;    // time the last touch was given
int timeout = 40;        // touch timeout window

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an
// int.
unsigned long lastDebounceTime = 0; //  last time the output pin was toggled
unsigned long debounceDelay = 5;    // debounce time, up if the outp flickers

// String msg = "";

uint8_t peerMac[6] = {0x4C, 0xC3, 0x82, 0x08, 0x90, 0x14};
// uint8_t peerMac[6] = { 0 };
bool peerConfigured = false;
int channel = 1;

typedef struct struct_message {
   char character[32];
   int touch;
   float floating_value;
} struct_message;
struct_message message;

void data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
   Serial.print("\r\nStatus of Last Message Sent:\t");
   Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void data_receive(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
   memcpy(&message, incomingData, sizeof(message));
   // Serial.print("Bytes received: ");
   // Serial.println(len);
   // Serial.print("Char: ");
   // Serial.println(message.character);
   // Serial.print("Float: ");
   // Serial.println(message.floating_value);
   // above can be removed it is helpful for demonstrating it works while
   // connected to a computer
   last_received = millis();
}

void setup() {
   Serial.begin(115200);
   pinMode(BTNPIN, OUTPUT);
   // Serial.println("on");

   WiFi.mode(WIFI_MODE_STA);
   // WiFi.begin();
   WiFi.disconnect();

   esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

   if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
   }
   esp_wifi_set_max_tx_power(84); // 84 = 21 dBm, maximum allowed

   esp_now_register_send_cb(data_sent);

   esp_now_peer_info_t peerInfo = {};
   esp_now_register_recv_cb(data_receive);

   memcpy(peerInfo.peer_addr, peerMac, 6);
   peerInfo.channel = channel;
   peerInfo.encrypt = false;
   if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      return;
   }

   Serial.println("setup complete");
}

void loop() {
   // touch = touchRead(T0);
   // // Serial.println(touch);
   // if (touch < 10) {
   //   // // Serial.println("guard hit");
   // }

   int reading = digitalRead(BTNPIN);

   if (reading != prevState) {
      lastDebounceTime = millis();
   }

   if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != currState) {
         currState = reading;
         // // Serial.println(reading);

         if (reading == HIGH) {
            strcpy(message.character, "hit");
            message.floating_value = millis();
            esp_err_t outcome = esp_now_send(peerMac, (uint8_t *)&message, sizeof(message));

            if (outcome == ESP_OK) {
               Serial.println("Mesage sent successfully!");
            } else {
               Serial.println("Error sending the message");
            }
            // digitalWrite(LEDPIN, HIGH);
            // Serial.println("Touch");
            // delay(1000);
            // digitalWrite(LEDPIN, LOW);

         } else {
            // digitalWrite(LEDPIN, LOW);
            // // Serial.println("OFF");
         }
      }

      // if (abs(touch - currTouch) > 2){
      //   currTouch = touch;
      //   Serial.println(touch);

      //   if (touch < 5) {
      //     strcpy(message.character, "grd");
      //     message.floating_value = millis();
      //     esp_err_t outcome = esp_now_send(peerMac, (uint8_t*)&message, sizeof(message));

      //     if (outcome == ESP_OK) {
      //       Serial.println("Mesage sent successfully!");
      //     } else {
      //       Serial.println("Error sending the message");
      //     }
      //     // Serial.println("guard hit");
      //   }
      // }
      // // Serial.println();
   }

   prevState = reading;
}
