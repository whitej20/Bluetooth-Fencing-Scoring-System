// server.ino
#include "WiFi.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// #4: B0:CB:D8:E2:FF:AC
// #5: 28:05:A5:32:B2:C4
uint8_t peerMac1[6] = {0xB0, 0xCB, 0xD8, 0xE2, 0xFF, 0xAC};
uint8_t peerMac2[6] = {0x28, 0x05, 0xA5, 0x32, 0xB2, 0xC4};
int channel = 1;

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

unsigned long firstHitTime = 0;
int firstFencer = -1;

SemaphoreHandle_t resetSemaphore = NULL;

typedef struct struct_message {
   char character[32];
   int touch;
   float floating_value;
} struct_message;
struct_message message;

typedef enum { IDLE, FIRST_HIT, LOCKED } ScoringState;
volatile ScoringState state = IDLE;

TaskHandle_t windowTaskHandle = NULL;

void processHit(int fencerIndex) {
   unsigned long now = millis(); // server receive time

   if (state == LOCKED) {
      Serial.println("Locked out, hit rejected");
      return;
   }

   if (state == IDLE) {
      // First hit
      firstFencer = fencerIndex;
      firstHitTime = now;
      state = FIRST_HIT;
      Serial.printf("First hit: fencer %d at %lums\n", fencerIndex, now);

      // Start window expiry task
      xTaskCreate(windowTimerTask, "win", 2048, NULL, 2, &windowTaskHandle);

   } else if (state == FIRST_HIT) {
      if (fencerIndex == firstFencer) {
         Serial.println("Same fencer duplicate, ignored");
         return;
      }

      // Second hit — evaluate
      unsigned long delta = now - firstHitTime;
      state = LOCKED;

      // Kill the window timer, no longer needed
      if (windowTaskHandle != NULL) {
         vTaskDelete(windowTaskHandle);
         windowTaskHandle = NULL;
      }

      Serial.printf("Second hit: fencer %d, delta=%lums\n", fencerIndex, delta);

      if (delta <= LOCKOUT_EPEE) {
         Serial.println("DOUBLE TOUCH");
         lightFencer(0);
         lightFencer(1);
      } else {
         Serial.printf("SINGLE TOUCH - fencer %d\n", firstFencer);
         lightFencer(firstFencer);
      }
   }
}

void windowTimerTask(void *pvParameters) {
   vTaskDelay(LOCKOUT_EPEE / portTICK_PERIOD_MS);

   if (state == FIRST_HIT) {
      state = LOCKED;
      Serial.printf("Window expired — fencer %d wins\n", firstFencer);
      lightFencer(firstFencer);
   }

   windowTaskHandle = NULL;
   vTaskDelete(NULL);
}

void lightFencer(int fencerIndex) {
   uint8_t msg = 1;
   xQueueOverwrite(ledQs[fencerIndex], &msg);
}

void resetScoring() {
   state = IDLE;
   firstFencer = -1;
   firstHitTime = 0;
   windowTaskHandle = NULL;
   Serial.println("--- RESET ---");
}

void resetTask(void *pvParameters) {
   while (1) {
      if (xSemaphoreTake(resetSemaphore, portMAX_DELAY)) {
         resetScoring();
      }
   }
}

void led_task(void *pvParameters) {
   int index = (int)pvParameters;
   int pin = pins[index];
   uint8_t dummy;

   while (1) {
      if (xQueueReceive(ledQs[index], &dummy, portMAX_DELAY)) {
         digitalWrite(pin, HIGH);
         vTaskDelay(LIGHT_TIMER / portTICK_PERIOD_MS);
         digitalWrite(pin, LOW);

         xSemaphoreGive(resetSemaphore);

         while (xQueueReceive(ledQs[index], &dummy, 0))
            ;
      }
   }
}

void data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
   Serial.print("\r\nStatus of Last Message Sent:\t");
   Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void data_receive(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
   memcpy(&message, incomingData, sizeof(message));

   if (strcmp(message.character, "hit") == 0) {
      int fencerInd = (memcmp(info->src_addr, peerMac1, 6) == 0) ? 0 : 1;
      processHit(fencerInd);
   }
}

void setup() {
   Serial.begin(115200);
   pinMode(LEDPIN_R, OUTPUT);
   pinMode(LEDPIN_L, OUTPUT);
   pinMode(GRDPIN_R, OUTPUT);
   pinMode(GRDPIN_L, OUTPUT);
   // Serial.println("on");

   WiFi.mode(WIFI_MODE_STA);
   // WiFi.begin();
   esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

   if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
   }
   // esp_wifi_set_max_tx_power(84); // 84 = 21 dBm, maximum allowed

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
   peer2.channel = channel;
   peer2.encrypt = false;
   if (esp_now_add_peer(&peer2) != ESP_OK) {
      Serial.println("Failed to add peer2");
      return;
   }

   resetSemaphore = xSemaphoreCreateBinary();
   xTaskCreate(resetTask, "reset", 2048, NULL, 1, NULL);

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