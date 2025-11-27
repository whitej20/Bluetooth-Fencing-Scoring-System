#include "WiFi.h"
//gets the mac address of the current esp32 board
void setup(){
  Serial.begin(115200);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin();
  delay(200);
  Serial.println();
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void loop(){

}