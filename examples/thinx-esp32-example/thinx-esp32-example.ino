#include <Arduino.h>
#include <THiNXLib32.h>

#define __DEBUG__
#define ARDUINO_IDE

const char *apikey = "132b5b3bb3d43c5ab6b01621b52f0484420219b418ba2bb8175f03c3f20db546";
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
