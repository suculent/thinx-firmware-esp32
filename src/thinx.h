//
// This is an auto-generated file, it will be re-written by THiNX on cloud build.
//

#define T_VERSION "2.9.450"

const char * THINX_ALIAS = "unnamed";
const char * THINX_API_KEY = "";
const char * THINX_APP_VERSION = "thinx-lib-esp32-arduino:" T_VERSION ":" __DATE__;
const char * THINX_CLOUD_URL = "app.thinx.cloud";
const char * THINX_COMMIT_ID = "0"; // serves as a safety check to prevent re-flashing with same commit ID, may deprecate in favour of flavouring
const char * THINX_FIRMWARE_VERSION = "thinx-lib-esp32-arduino:" T_VERSION;
const char * THINX_FIRMWARE_VERSION_SHORT = T_VERSION;
const char * THINX_OWNER = "0";
const char * THINX_MQTT_URL = "thinx.cloud";
const char * THINX_PLATFORM  = "platformio";
const char * THINX_UDID = "0";

const long THINX_MQTT_PORT = 1883;
const long THINX_MQTTS_PORT = 8883;
const long THINX_API_PORT = 443; // 7442->80; 7443->443 in Swarm

const bool THINX_AUTO_UPDATE = true;
const bool THINX_FORCED_UPDATE = false;

// THiNX CI appends Environment Variables here:

const char * THINX_ENV_SSID = "";     // your network SSID (name); better use DevSec instead
const char * THINX_ENV_PASS = "";     // your network password; better use DevSec instead

// Hash of the environment, allows force update with same firmware version
const char * ENV_HASH = "cafebabe";
//const char * ENV_HASH = "";           // empty for no environment hashes