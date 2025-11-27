#include "WiFi.h"
#include <esp_now.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>

const int LEDPIN = 25;  // pin for LED and buzzer
const int BTNPIN = 13;  // pin to body cord b wire

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
unsigned long debounceDelay = 5;     // the debounce time; increase if the output flickers

// 4C:C3:82:08:90:14 : Fencer 2 mac address
// 4C:C3:82:08:90:E0 : Fencer 1 mac address

// REPLACE WITH YOUR RECEIVER MAC Address
// uint8_t peerMac[] = { 0x4C, 0xC3, 0x82, 0x08, 0x90, 0x14 };  // 4C:C3:82:08:90:14 : Fencer 2 mac address
// uint8_t peerMac[] = {0x4C, 0xC3, 0x82, 0x08, 0x90, 0xE0}; // 4C:C3:82:08:90:E0 : Fencer 1 mac address
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

// void data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) { //old
// version
void data_sent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  // info->mac give mac addr if needed
  // Serial.print("\r\nStatus of Last Message Sent:\t");
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

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
    // Serial.println("Failed to add peer");
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


  WiFi.begin("baijin eero", "49Kingsland");
  // Serial.print("Connecting");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    // Serial.print(".");
  }
  // Serial.println("\nConnected!");
  // Serial.print(mac);
  // Serial.println(" online!");
  
  channel = WiFi.channel();
  mac = WiFi.macAddress();
  loadPeer();
  
  // Serial.print("wifi channel: ");
  // Serial.println(channel);

  // sendUdpMsg("Fencer " + mac + "online!");

  if (esp_now_init() != ESP_OK) {
    // Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(data_sent);
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
  
  // esp_now_peer_info_t peerInfo = {};

  // memcpy(peerInfo.peer_addr, peerMac, 6);
  // peerInfo.channel = channel;
  // peerInfo.encrypt = false;
  // if (esp_now_add_peer(&peerInfo) != ESP_OK) {
  //   // Serial.println("Failed to add peer");
  //   return;
  // }

  // pinMode(LEDPIN, OUTPUT);
  // pinMode(BTNPIN, INPUT);
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

      // only toggle the LED if the new button state is HIGH
      if (reading == HIGH) {
        last_given = millis();
        strcpy(message.character, "Fencer 1 hit");
        message.floating_value = millis();
        esp_err_t outcome =
          esp_now_send(peerMac, (uint8_t*)&message, sizeof(message));

        if (outcome == ESP_OK) {
          // Serial.println("Mesage sent successfully!");
        } else {
          // Serial.println("Error sending the message");
        }

        score();
        // } else {
        //   digitalWrite(LEDPIN, LOW);
        //   ledState = LOW;
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
    msg = mac + "hit";
    sendUdpMsg(msg);
    delay(1000);

    digitalWrite(LEDPIN, LOW);

  }
}
