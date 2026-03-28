#include <esp_now.h>
#include <WiFi.h>
#include "WiFi.h"
#include <esp_wifi.h> 

// #4: B0:CB:D8:E2:FF:AC
// #5: 28:05:A5:32:B2:C4
uint8_t peerMac1[6] = {0xB0, 0xCB, 0xD8, 0xE2, 0xFF, 0xAC};
uint8_t peerMac2[6] = {0x28, 0x05, 0xA5, 0x32, 0xB2, 0xC4};
int channel = 0;

float last_received = 0;  // time the last touch was received
float last_given = 0;     // time the last touch was given

typedef struct struct_message {
  char character[32];
  float floating_value;
} struct_message;
struct_message message;

void data_sent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  Serial.print("\r\nStatus of Last Message Sent:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void data_receive(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
  memcpy(&message, incomingData, sizeof(message));
  // Serial.print("Bytes received: ");
  // Serial.println(len);
  // Serial.print("Char: ");
  // Serial.println(message.character);
  // Serial.print("Float: ");
  // Serial.println(message.floating_value);
  //above can be removed it is helpful for demonstrating it works while connected to a computer


  Serial.print("From MAC: ");

  for (int i = 0; i < 6; i++) {
    Serial.print(info->src_addr[i], HEX);
    if (i < 5) Serial.print(":");
  }


  Serial.print("\nMessage: ");
  Serial.println(message.character);
  Serial.println();

  last_received = millis();
}

void setup() {
  Serial.begin(115200);
  // pinMode(LEDPIN, OUTPUT);
  // Serial.println("on");

  WiFi.mode(WIFI_MODE_STA);
  // WiFi.begin();

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(data_sent);

  esp_now_peer_info_t peer1 = {};
  esp_now_peer_info_t peer2 = {};
  esp_now_register_recv_cb(data_receive);

  

  memcpy(peer1.peer_addr, peerMac1, 6);
  peer1.channel = channel;
  peer1.encrypt = false;
  if (esp_now_add_peer(&peer1) != ESP_OK){
    Serial.println("Failed to add peer1");
    return;
  }

  memcpy(peer2.peer_addr, peerMac2, 6);
  peer2.channel = 0;
  peer2.encrypt = false;
  if (esp_now_add_peer(&peer2) != ESP_OK){
    Serial.println("Failed to add peer2");
    return;
  }

  Serial.println("setup complete");

}

void loop(){

}