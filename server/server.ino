// server.ino
#include "WiFi.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// #4: B0:CB:D8:E2:FF:AC
// #5: 28:05:A5:32:B2:C4
uint8_t peerMac1[6] = {0xB0, 0xCB, 0xD8, 0xE2, 0xFF, 0xAC};
uint8_t peerMac2[6] = {0x28, 0x05, 0xA5, 0x32, 0xB2, 0xC4};
int channel = 0;

float last_received = 0; // time the last touch was received
float last_given = 0;    // time the last touch was given

const int GRDPIN_L = 27;
const int GRDPIN_R = 13;
const int LEDPIN_L = 14;
const int LEDPIN_R = 12;

// TimerHandle_t ledTimers[4];
QueueHandle_t ledQs[4];
const int pins[4] = {LEDPIN_R, LEDPIN_L, GRDPIN_R, GRDPIN_L};

unsigned long hitTime[2] = {0, 0};     // time each fencer last hit
const unsigned long LOCKOUT_EPEE = 40; // FIE epee double-touch window
bool hitLocked[2] = {false, false};

const int LIGHT_TIMER = 2000; // how long light lasts

typedef struct struct_message {
   char character[32];
   int touch;
   float floating_value;
} struct_message;
struct_message message;

bool doubleIsValid(int fencerIndex) {
   unsigned long now = millis();
   int other = 1 - fencerIndex; // 0->1, 1->0

   // if this fencer is locked out, reject
   if (hitLocked[fencerIndex])
      return false;

   // lock this fencer out
   hitLocked[fencerIndex] = true;
   hitTime[fencerIndex] = now;

   // if the other fencer hit recently (within window), that's a double touch — don't lock them out
   // if outside window, lock the other fencer out too (they missed their chance)
   // if (!hitLocked[other] || (now - hitTime[other]) > LOCKOUT_EPEE) {
   //    hitLocked[other] = false; // reset other if window expired
   // }

   return true;
}

// void led_timer_cb(TimerHandle_t xTimer){
//    int index = (int)pvTimerGetTimerID(xTimer);
//    digitalWrite(pins[index], LOW);
// }

void led_task(void *pvParameters) {
   int index = (int)pvParameters;
   int pin = pins[index];
   uint8_t dummy;
   int fencerIndex = index % 2; // 0,2 -> fencer 0; 1,3 -> fencer 1

   while (1) {
      if (xQueueReceive(ledQs[index], &dummy, portMAX_DELAY)) {
         digitalWrite(pin, HIGH);
         vTaskDelay(LIGHT_TIMER / portTICK_PERIOD_MS);
         digitalWrite(pin, LOW);

         hitLocked[fencerIndex] = false;

         // drain any messages that piled up
         while (xQueueReceive(ledQs[index], &dummy, 0))
            ;

         // xTimerReset(ledTimers[index], 0); // if timer running, reset
      }
   }
}


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
   // above can be removed it is helpful for demonstrating it works while connected to a computer
   Serial.print("From MAC: ");
   for (int i = 0; i < 6; i++) {
      Serial.print(info->src_addr[i], HEX);
      if (i < 5)
         Serial.print(":");
   }

   Serial.print("\nMessage: ");
   Serial.println(message.character);
   Serial.println();

   int pin = -1;

   if (strcmp(message.character, "hit") == 0) {
      if (memcmp(info->src_addr, peerMac1, 6) == 0) {
         // pin = LEDPIN_R;
         pin = 0;
      } else if (memcmp(info->src_addr, peerMac2, 6) == 0) {
         // pin = LEDPIN_L;
         pin = 1;
      }

   } else if (strcmp(message.character, "grd") == 0) {
      if (memcmp(info->src_addr, peerMac1, 6) == 0) {
         // pin = GRDPIN_R;
         pin = 2;
      } else if (memcmp(info->src_addr, peerMac2, 6) == 0) {
         // pin = GRDPIN_L;
         pin = 3;
      }
   }

   if (pin != -1) {
      // xTaskCreate(blink_task, "blink", 1024, (void *)pin, 1, NULL);
      int fencerInd = (memcmp(info->src_addr, peerMac1, 6) == 0) ? 0 : 1;

      if (doubleIsValid(fencerInd)) {
         uint8_t msg = 1;
         xQueueOverwrite(ledQs[pin], &msg);
      }
   }

   last_received = millis();
}

void setup() {
   Serial.begin(115200);
   pinMode(LEDPIN_R, OUTPUT);
   pinMode(LEDPIN_L, OUTPUT);
   pinMode(GRDPIN_R, OUTPUT);
   pinMode(GRDPIN_L, OUTPUT);
   // Serial.println("on");

   WiFi.mode(WIFI_MODE_STA);
   WiFi.begin();

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
   if (esp_now_add_peer(&peer1) != ESP_OK) {
      Serial.println("Failed to add peer1");
      return;
   }

   memcpy(peer2.peer_addr, peerMac2, 6);
   peer2.channel = 0;
   peer2.encrypt = false;
   if (esp_now_add_peer(&peer2) != ESP_OK) {
      Serial.println("Failed to add peer2");
      return;
   }

   for (int i = 0; i < 4; i++) {
      // int timer = (i < 2) ? 1000 : 100;
      // int timer = 1000;

      ledQs[i] = xQueueCreate(1, sizeof(uint8_t)); // depth 1 = only 1 pending
      // ledTimers[i] = xTimerCreate("led_off", pdMS_TO_TICKS(timer), pdFALSE, (void*)i, led_timer_cb);
      xTaskCreate(led_task, "led", 1024, (void *)i, 1, NULL);
   }

   digitalWrite(LEDPIN_L, HIGH);
   digitalWrite(LEDPIN_R, HIGH);
   digitalWrite(GRDPIN_L, HIGH);
   digitalWrite(GRDPIN_R, HIGH);
   delay(200);
   digitalWrite(LEDPIN_L, LOW);
   digitalWrite(LEDPIN_R, LOW);
   digitalWrite(GRDPIN_L, LOW);
   digitalWrite(GRDPIN_R, LOW);
   delay(200);
   digitalWrite(LEDPIN_L, HIGH);
   digitalWrite(LEDPIN_R, HIGH);
   digitalWrite(GRDPIN_L, HIGH);
   digitalWrite(GRDPIN_R, HIGH);
   delay(200);
   digitalWrite(LEDPIN_L, LOW);
   digitalWrite(LEDPIN_R, LOW);
   digitalWrite(GRDPIN_L, LOW);
   digitalWrite(GRDPIN_R, LOW);

   Serial.println("setup complete");
}

void loop() {}