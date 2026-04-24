// clientTouch.ino
#include "WiFi.h"
#include <WiFi.h>
#include <cmath>
#include <esp_now.h>
#include <esp_wifi.h>

long bladeSum = 0;
int bladeAvg = 0;

// A wire -> T0
// B wire -> GND
// C wire -> 1Mohm -> GND
const int TCHPIN_A = 4;  // T0
const int TCHPIN_C = 15; // T3

int channel = 1;
uint8_t peerMac[6] = {0x4C, 0xC3, 0x82, 0x08, 0x90, 0x14};

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
   //    pinMode(BTNPIN, OUTPUT);
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
   int loop = 10;
   bladeSum = 0;
   for (int i = 0; i < loop; i++) {
      bladeSum += touchRead(TCHPIN_A);
   }
   bladeAvg = bladeSum / loop;

   if (bladeAvg < 15) {
      int grdCheck = touchRead(TCHPIN_C);

      if (grdCheck > (bladeAvg + 20)) {
         Serial.println("hit");
         strcpy(message.character, "hit");
      } else {
         Serial.println("grd");
         strcpy(message.character, "grd");
      }

      message.floating_value = millis();
      esp_err_t outcome = esp_now_send(peerMac, (uint8_t *)&message, sizeof(message));

      if (outcome == ESP_OK) {
         Serial.println("Mesage sent successfully!");
      } else {
         Serial.println("Error sending the message");
      }
   }

   delay(500);
}