#include <Arduino.h>
#include <THiNX32Lib.h>

#define ARDUINO_IDE


// Import SSID, PASSWORD and API KEY from file not included in repository
#ifndef THINXCI
#import "settings.h"
#else
// Those are just dummy demo values. Never store those in public repository!
// Save them to settings.h and use .gitigore to keep file on your machine only.
// THiNX CI will be able to add the apikey and owner_id on its own. You can configure
// optinally THINX_ENV_SSID and THINX_ENV_PASS Environment Variables to inject those from CI.

// Add this to your settings.h and ignore this file from repository before committing!
const char *apikey = "";
const char *owner_id = "";
const char *ssid = "THiNX-IoT+";
const char *pass = "<enter-your-ssid-password>";
#endif

THiNX thx;

/* Called after library gets connected and registered */
void finalizeCallback () {
  Serial.println("*INO: Finalize callback called.");
  ESP.deepSleep(3e9);
}

void setup() {

  Serial.begin(115200);

#ifdef __DEBUG__
  while (!Serial); // wait for debug console connection
  WiFi.begin(ssid, pass);
#endif

  thx = THiNX(apikey);

  // Enter API Key and Owner ID (should be faster)
  //thx = THiNX(apikey, owner_id);
  thx.setFinalizeCallback(finalizeCallback);
}

/* Loop must call the thx.loop() in order to pickup MQTT messages and advance the state machine. */
void loop()
{
  thx.loop();
}
