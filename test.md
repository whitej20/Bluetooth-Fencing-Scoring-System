```c
// All timing done with server-side millis() on receive
// No sync needed

unsigned long firstHitReceiveTime = 0;
int firstFencer = -1;

typedef enum {
    STATE_IDLE,
    STATE_FIRST_HIT,
    STATE_LOCKED
} ScoringState;

volatile ScoringState state = STATE_IDLE;

void processHit(int fencerIndex) {
    unsigned long now = millis(); // server receive time

    if (state == STATE_LOCKED) {
        Serial.println("Locked out, hit rejected");
        return;
    }

    if (state == STATE_IDLE) {
        // First hit
        firstFencer = fencerIndex;
        firstHitReceiveTime = now;
        state = STATE_FIRST_HIT;
        Serial.printf("First hit: fencer %d at %lums\n", fencerIndex, now);

        // Start window expiry task
        xTaskCreate(windowTimerTask, "win", 2048, NULL, 2, &windowTaskHandle);

    } else if (state == STATE_FIRST_HIT) {
        if (fencerIndex == firstFencer) {
            Serial.println("Same fencer duplicate, ignored");
            return;
        }

        // Second hit — evaluate
        unsigned long delta = now - firstHitReceiveTime;
        state = STATE_LOCKED;

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
            Serial.printf("SINGLE TOUCH — fencer %d\n", firstFencer);
            lightFencer(firstFencer);
        }
    }
}

TaskHandle_t windowTaskHandle = NULL;

void windowTimerTask(void *pvParameters) {
    vTaskDelay(LOCKOUT_EPEE / portTICK_PERIOD_MS);

    if (state == STATE_FIRST_HIT) {
        state = STATE_LOCKED;
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
    state = STATE_IDLE;
    firstFencer = -1;
    firstHitReceiveTime = 0;
    windowTaskHandle = NULL;
    Serial.println("--- RESET ---");
}