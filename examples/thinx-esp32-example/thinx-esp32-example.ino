#include <Arduino.h>
#include <THiNXLib32.h>

#define __DEBUG__
#define ARDUINO_IDE

const char *apikey = "4721f08a6df1a36b8517f678768effa8b3f2e53a7a1934423c1f42758dd83db5";
const char *owner_id = "cedc16bb6bb06daaa3ff6d30666d91aacd6e3efbf9abbc151b4dcade59af7c12";
const char *ssid = "THiNX-IoT-X";
const char *pass = "<enter-your-ssid-password>";

THiNX thx;

/* Called after library gets connected and registered */
void finalizeCallback () {
  Serial.println("*INO: Finalize callback called. Will fall asleep.");
  ESP.deepSleep(3e9);
}

void setup() {

  Serial.begin(230400);

#ifdef __DEBUG__
  while (!Serial); // wait for debug console connection
  Serial.println("Connecting to predefined WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  delay(2000);
#endif

  // Enter API Key and Owner ID (should be faster)
  thx = THiNX(apikey, owner_id);
  thx.setFinalizeCallback(finalizeCallback);
}

/* Loop must call the thx.loop() in order to pickup MQTT messages and advance the state machine. */
void loop()
{
  thx.loop();
}
