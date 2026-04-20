```cpp
#include "WiFi.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

uint8_t peerMac1[6] = {0xB0, 0xCB, 0xD8, 0xE2, 0xFF, 0xAC};
uint8_t peerMac2[6] = {0x28, 0x05, 0xA5, 0x32, 0xB2, 0xC4};
int channel = 1;

const int GRDPIN_L = 27;
const int GRDPIN_R = 13;
const int LEDPIN_L = 14;
const int LEDPIN_R = 12;

QueueHandle_t ledQs[4];
const int pins[4] = {LEDPIN_R, LEDPIN_L, GRDPIN_R, GRDPIN_L};

const unsigned long LOCKOUT_EPEE = 40;
const int LIGHT_TIMER = 2000;

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

// BLE globals — pointers, not value types
BLECharacteristic *txChar = nullptr;
bool bleConnected = false;

// -- BLE callbacks — class ends at its own closing brace, nothing else inside --
class BLEConnectionCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override {
        bleConnected = true;
        Serial.println("[BLE] Client connected");
    }
    void onDisconnect(BLEServer *server) override {
        bleConnected = false;
        Serial.println("[BLE] Client disconnected, restarting advertising");
        BLEDevice::startAdvertising();
    }
};

// -- setupBLE is a free function, NOT inside the class --
void setupBLE() {
    BLEDevice::init("FencingScorer");
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new BLEConnectionCallbacks());

    BLEService *service = server->createService(NUS_SERVICE_UUID);

    txChar = service->createCharacteristic(
        NUS_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    txChar->addDescriptor(new BLE2902());

    BLECharacteristic *rxChar = service->createCharacteristic(
        NUS_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );

    service->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising started");
}

void bleSend(const char *msg) {
    if (bleConnected && txChar != nullptr) {
        txChar->setValue((uint8_t *)msg, strlen(msg));
        txChar->notify();
        Serial.printf("[BLE] Sent: %s\n", msg);
    }
}

// forward declarations so processHit can call lightFencer and windowTimerTask
void lightFencer(int fencerIndex);
void windowTimerTask(void *pvParameters);

void processHit(int fencerIndex) {
    unsigned long now = millis();

    if (state == LOCKED) {
        Serial.println("Locked out, hit rejected");
        return;
    }

    if (state == IDLE) {
        firstFencer = fencerIndex;
        firstHitTime = now;
        state = FIRST_HIT;
        Serial.printf("First hit: fencer %d at %lums\n", fencerIndex, now);
        xTaskCreate(windowTimerTask, "win", 2048, NULL, 2, &windowTaskHandle);

    } else if (state == FIRST_HIT) {
        if (fencerIndex == firstFencer) {
            Serial.println("Same fencer duplicate, ignored");
            return;
        }

        unsigned long delta = now - firstHitTime;
        state = LOCKED;

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
    bleSend(fencerIndex == 0 ? "r_hit" : "l_hit");
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
            while (xQueueReceive(ledQs[index], &dummy, 0));
        }
    }
}

void data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
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

    WiFi.mode(WIFI_MODE_STA);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(data_sent);
    esp_now_register_recv_cb(data_receive);

    esp_now_peer_info_t peer1 = {};
    memcpy(peer1.peer_addr, peerMac1, 6);
    peer1.channel = channel;
    peer1.encrypt = false;
    if (esp_now_add_peer(&peer1) != ESP_OK) {
        Serial.println("Failed to add peer1");
        return;
    }

    esp_now_peer_info_t peer2 = {};
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
        ledQs[i] = xQueueCreate(1, sizeof(uint8_t));
        xTaskCreate(led_task, "led", 1024, (void *)i, 1, NULL);
    }

    setupBLE();

    // startup flash
    for (int i = 0; i < 2; i++) {
        digitalWrite(LEDPIN_L, HIGH); digitalWrite(LEDPIN_R, HIGH);
        digitalWrite(GRDPIN_L, HIGH); digitalWrite(GRDPIN_R, HIGH);
        delay(200);
        digitalWrite(LEDPIN_L, LOW);  digitalWrite(LEDPIN_R, LOW);
        digitalWrite(GRDPIN_L, LOW);  digitalWrite(GRDPIN_R, LOW);
        delay(200);
    }

    Serial.println("setup complete");
}

void loop() {}
```