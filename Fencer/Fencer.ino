#include "WiFi.h"
#include <esp_now.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include "../config.h"

const int LEDPIN = 25;  // pin for LED and buzzer
const int BTNPIN = 13;  // pin to body cord b wire
const int GRDPIN = 27;  // pin to body cord c wire (ground/bell guard wire)

int timeout = 40;  // touch timeout window

// Variables will change:
int ledState = LOW;       // the current state of the output pin
int currState;            // the current reading from the input pin
int prevState = LOW;      // the previous reading from the input pin
float last_received = 0;  // time the last touch was received
float last_given = 0;     // time the last touch was given

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an
// int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 3;     // the debounce time; increase if the output flickers

// peer will be loaded from memory. if no such peer exists, it will wait to receive a udp packet giving the target
uint8_t peerMac[6] = {0};
bool peerConfigured = false;
int channel = 0;

typedef struct struct_message {
  char character[100];
  float floating_value;
} struct_message;
struct_message message;

WiFiUDP udp;
String mac; // self mac addr as string
int port = 6969;
Preferences prefs; 

String msg = "";

bool isSignalClean = true;

// void data_receive(const uint8_t * mac, const uint8_t *incomingData, int len)
// { //old
void data_receive(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
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

void sendUdpMsg(String msg){
  udp.beginPacket("255.255.255.255", port);
  udp.print(msg);
  udp.endPacket();
}

// load existing peer mac addr from flash mem
void loadPeer(){
  prefs.begin("fencing", false);
  
  if (prefs.isKey("peerMac")) {
    size_t len = prefs.getBytes("peerMac", peerMac, 6);
    if (len == 6){
      // check to make sure not 00:00:00:00:00:00
      bool hasVal = false;
      for(int i = 0; i < 6; i++){
        if(peerMac[i] != 0){
          hasVal = true;
          break;
        }
      }

      if(hasVal){
        peerConfigured = true;
        // msg = "Successfully loaded peer: " + peerMac[0] + peerMac[1] + peerMac[2] + peerMac[3] + peerMac[4] + peerMac[5];
        // Serial.println(msg);
      }

    }
  }

  if (!peerConfigured){
    // Serial.println("No peer saved, awaiting configuration");
  }

  prefs.end();

}

// save peer to flash memory
void savePeer(uint8_t* pMac){
  prefs.begin("fencing", false);
  prefs.putBytes("peerMac", pMac, 6);
  prefs.end();

  // msg = "Successfully saved peer: " + pMac[0] + pMac[1] + pMac[2] + pMac[3] + pMac[4] + pMac[5];
  // Serial.println(msg);
}

// add ESP now peer
void addPeer(){
  esp_now_peer_info_t peerInfo = {};

  memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = channel;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

void awaitConfig(){
  udp.begin(port);

  bool ledOn = false;
  unsigned long lastBlink = 0;

  while(!peerConfigured){
    if(millis() - lastBlink > 500){
      ledOn = !ledOn;
      digitalWrite(LEDPIN, ledOn ? HIGH : LOW);
      lastBlink = millis();
    }

    int packetSize = udp.parsePacket();
    if(packetSize){
      char incoming[255];
      int len = udp.read(incoming, 255);
      
      if(len > 0){
        incoming[len] = 0;
      }

      String cmd = String(incoming);
      cmd.trim();
      // Serial.println("Received: " + cmd);

      if(cmd.startsWith("SETPEER:")){
        String macStr = cmd.substring(8);
        macStr.trim();

        uint8_t newMac[6];
        if (sscanf(macStr.c_str(), "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                   &newMac[0], &newMac[1], &newMac[2],
                   &newMac[3], &newMac[4], &newMac[5]) == 6) {

          memcpy(peerMac, newMac, 6);
          peerConfigured = true;
          savePeer(peerMac);
          loadPeer;

          sendUdpMsg("ACK:" + mac + ":PEERSET");
          
          // Serial.println("Config complete!");
          digitalWrite(LEDPIN, LOW);
          
        } else {
          // Serial.println("Invalid MAC format");
          sendUdpMsg("ERROR:" + mac + ":BADMAC");
        }
      }
    
    
    }
    
  }

  udp.stop();
}

void setup() {
  // Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  // for some reason setting this makes it behave weirdly (lights up when i touch the wire, etc)
  // it works properly when i dont set it so i will simply leave it ¯\_(ツ)_/¯
  // pinMode(BTNPIN, INPUT);
  pinMode(LEDPIN, OUTPUT);

  digitalWrite(LEDPIN, HIGH);
  delay(2000);  // LED should be on for 2 seconds
  digitalWrite(LEDPIN, LOW);
  // Serial.println("Lights and sound test complete");

  WiFi.begin();
  // WiFi.begin(wifiSsid, wifiPass);
  // // Serial.print("Connecting");
  
  // // block until connected to wifi
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(200);
  //   // Serial.print(".");
  // }
  // Serial.println("\nConnected!");
  // Serial.print(mac);
  // Serial.println(" online!");
  
  // channel = WiFi.channel();
  mac = WiFi.macAddress();
  loadPeer();
  
  // Serial.print("wifi channel: ");
  // Serial.println(channel);

  if (esp_now_init() != ESP_OK) {
    // Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(data_receive);
  
  if(peerConfigured){
    addPeer();
  }
  
  msg = "ONLINE: " + mac + "\n\tTARGET: ";
  if(peerConfigured){
    char peerStr[18];
    snprintf(peerStr, sizeof(peerStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                peerMac[0], peerMac[1], peerMac[2], 
                peerMac[3], peerMac[4], peerMac[5]
    );
    msg += String(peerStr);
  }else{
    msg += "NONE";
  }

  sendUdpMsg(msg);

  // block until configured
  if(!peerConfigured){
    awaitConfig();
  }

  // begin bell guard oscillation
  // guard pin, 1000Hz, 8 bit res
  // ledcAttach(GRDPIN, 2, 8);
  // ledcWrite(GRDPIN, 128); // 8 bit = 0-255, 128 is half so equal on/off

  // pinMode(GRDPIN, OUTPUT);
  // digitalWrite(GRDPIN, HIGH);

}

void loop() {
  // if condition checks if push button is pressed
  // if pressed LED will turn on otherwise remain off
  int reading = digitalRead(BTNPIN);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH), and you've waited long enough
  // since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != prevState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != currState) {
      currState = reading;

      // sample 10x times in 1ms to check if signal is clean
      // not in a function to reduce overhead
      // int highCount;
      // for(int i = 0; i < 10; i++){
      //   if (digitalRead(BTNPIN) == HIGH){
      //     highCount++;
      //   }
      //   delayMicroseconds(100);
      // }

      // if(highCount >= 9) { // allow 1 sample tolerance
      //   isSignalClean = true;
      // } else{
      //   isSignalClean = false;
      // }

      // only toggle the LED if the new button state is HIGH
      if (reading == HIGH) {

        // if(!isSignalClean){
        //   currState = LOW;
        //   sendUdpMsg("GROUND: " + mac);
        // } else{
          last_given = millis();
          strcpy(message.character, "hit");
          message.floating_value = millis();
          esp_err_t outcome = esp_now_send(peerMac, (uint8_t*)&message, sizeof(message));
        // }


        // if (outcome == ESP_OK) {
        //   Serial.println("Mesage sent successfully!");
        // } else {
        //   Serial.println("Error sending the message");
        // }

        score();
        
      }
    }
  }

  // save the reading. Next time through the loop, it'll be the prevState:
  prevState = reading;
}

void score() {
  // if the touch has not been timed out
  if (abs(last_received - last_given) < timeout || abs(last_received - last_given) > 2000) {

    digitalWrite(LEDPIN, HIGH);

    // Serial.println("Point scored");
    sendUdpMsg("POINT: " + mac);
    delay(1000);

    digitalWrite(LEDPIN, LOW);

  }
}
