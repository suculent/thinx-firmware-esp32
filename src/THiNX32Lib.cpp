extern "C" {
  #include "thinx.h"
  #include <time.h>
  #include <stdlib.h>
}

#include "THiNX32Lib.h"

#ifndef UNIT_TEST // IMPORTANT LINE FOR UNIT-TESTING!

#ifndef THINX_FIRMWARE_VERSION_SHORT
#define THINX_FIRMWARE_VERSION_SHORT VERSION
#endif

#ifndef THINX_COMMIT_ID
// any commit ID is sufficient to allow update
#define THINX_COMMIT_ID "0c48a9ab0c4f89c4b8fb72173553d3e74986632d0"
#endif

// Static variables
char* THiNX::thinx_api_key;
char* THiNX::thinx_owner_key;
bool  THiNX::forceHTTP = false;
char  THiNX::json_buffer[512];

const char THiNX::time_format[] = "%T";
const char THiNX::date_format[] = "%Y-%m-%d";
char * THiNX::thinx_mqtt_url = strdup(THINX_MQTT_URL);

#include "thinx_root_ca.h"

#ifdef __USE_WIFI_MANAGER__
char THiNX::thx_api_key[65] = {0};
char THiNX::thx_owner_key[65] = {0};
int  THiNX::should_save_config = 0;

WiFiManagerParameter * THiNX::api_key_param;
WiFiManagerParameter * THiNX::owner_param;

void THiNX::saveConfigCallback() {
  //if (logging) Serial.println(F("* TH: WiFiManager configuration should be saved now!"));
  should_save_config = true;
  strcpy(thx_api_key, api_key_param->getValue());
  strcpy(thx_owner_key, owner_param->getValue());
}
#endif

double THiNX::latitude = 0.0;
double THiNX::longitude = 0.0;
String THiNX::statusString = "Registered";
String THiNX::accessPointName = "THiNX-AP";
String THiNX::accessPointPassword = "PASSWORD";
String THiNX::lastWill = "{ \"status\" : \"disconnected\" }";

bool   THiNX::logging = false;

uint32_t THiNX::last_free_heap_size;

/* Convenience method for debugging memory issues. */
void THiNX::printStackHeap(String tag) {
  uint32_t heap = ESP.getFreeHeap();
  uint32_t diff = 0;
  String way = "=";
  if (last_free_heap_size != heap) {
    if (last_free_heap_size > heap) {
      diff = last_free_heap_size - heap;
      way = "-";
    } else {
      diff = heap - last_free_heap_size;
      way = "+";
    }
    if (diff < 128) return;
    Serial.print(tag); Serial.print("HEAP: "); Serial.print(heap); Serial.print(" delta "); Serial.print(way); Serial.println(diff);
    last_free_heap_size = heap;
  }
}

/* Constructor */

THiNX::THiNX() {

}

/* Designated Initializers */

THiNX::THiNX(const char * __apikey) {

  THiNX(__apikey, "");
}

THiNX::THiNX(const char * __apikey, const char * __owner_id) {

  thinx_phase = INIT;

  #ifdef __USE_WIFI_MANAGER__
  should_save_config = false;
  WiFiManager wifiManager;
  api_key_param = new WiFiManagerParameter("apikey", "API Key", thinx_api_key, 64);
  wifiManager.addParameter(api_key_param);
  owner_param = new WiFiManagerParameter("owner", "Owner ID", thinx_owner_key, 64);
  wifiManager.addParameter(owner_param);
  wifiManager.setTimeout(5000);
  wifiManager.setDebugOutput(true); // does some logging on mode set
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.autoConnect(accessPointName.c_str());
  #endif

  Serial.print(F("\nTHiNXLib v"));
  Serial.print(VERSION);
  Serial.print(F(" rev. "));
  Serial.println(THX_REVISION);

#ifdef DEBUG
  // see lines ../hardware/cores/esp8266/Esp.cpp:80..100
  //wdt_disable(); // causes wdt reset after 8 seconds!
  //wdt_enable(60000); // must be called from wdt_disable() state!
#endif

  if (once != true) {
    once = true;
  }

  status = WL_IDLE_STATUS;
  wifi_connected = false;
  mqtt_client = NULL;
  mqtt_connected = false;
  performed_mqtt_checkin = false;
  wifi_connection_in_progress = false;
  wifi_retry = 0;

  app_version = strdup("");
  available_update_url = strdup("");
  thinx_cloud_url = strdup("thinx.cloud");

  thinx_firmware_version_short = strdup("");
  thinx_firmware_version = strdup("");
  thinx_mqtt_url = strdup("thinx.cloud");
  thinx_version_id = strdup("");
  thinx_api_key = strdup("");
  thinx_forced_update = false;
  last_checkin_timestamp = 0; // 1/1/1970

  checkin_time = millis() + checkin_interval / 4; // retry faster before first checkin
  reboot_interval = millis() + reboot_timeout;

  deferred_update_url = ""; // may be loaded from device info or set from registration

  // will be loaded from SPIFFS/EEPROM or retrieved on Registration later
  if (strlen(__owner_id) == 0) {
    thinx_owner = strdup("");
  }

  EEPROM.begin(512); // should be SPI_FLASH_SEC_SIZE

  import_build_time_constants();
  restore_device_info();
  info_loaded = true;

  #ifdef __USE_WIFI_MANAGER__
  wifi_connected = true;
  #else
  if ((WiFi.status() == WL_CONNECTED) && (WiFi.getMode() == WIFI_STA)) {
    wifi_connected = true;
    wifi_connection_in_progress = false;
  } else {
    WiFi.mode(WIFI_STA);
  }
  #endif

  if (strlen(__apikey) > 4) {
    Serial.println(F("*TH: With custom API Key..."));
    thinx_api_key = strdup(__apikey);
  } else {
      if (strlen(thinx_api_key) > 4) {
      } else {
#ifdef DEBUG
          if (logging) Serial.print(F("*TH: No API Key!"));
#endif
          return;
      }
  }

  if (strlen(__owner_id) > 4) {
    thinx_owner = strdup(__owner_id);
  } else {
      if (strlen(thinx_owner) < 4) {
#ifdef DEBUG
          if (logging) Serial.print(F("*TH: No API Key!"));
#endif
          return;
      }
  }

  init_with_api_key(thinx_api_key);
  wifi_connection_in_progress = false; // last
}

// Designated initializer
void THiNX::init_with_api_key(const char * __apikey) {

  #ifdef __USE_SPIFFS__
  //if (logging) Serial.println(F("*TH: Checking filesystem, please wait..."));
  if (!fsck()) {
    if (logging) Serial.println(F("*TH: Filesystem check failed, disabling THiNX."));
    return;
  }
  #endif

  if (info_loaded == false) {
    restore_device_info(); // loads saved apikey/ownerid
    info_loaded = true;
  }

  if (strlen(__apikey) > 4) {
    thinx_api_key = strdup(__apikey);
  } else {
    if (strlen(thinx_api_key) < 4) {
#ifdef DEBUG
      if (logging) Serial.print(F("*TH: No API Key!"));
#endif
      return;
    }
  }

  wifi_connection_in_progress = false;
  thinx_phase = CONNECT_WIFI;
}

/*
* Connection management
*/

char* THiNX::get_udid() {
  return strdup(thinx_udid);
}

void THiNX::connect() {

  if (wifi_connected) {
    return;
  }

#ifdef DEBUG
  if (logging) {
    Serial.print(F("*TH: connecting: "));
    Serial.println(wifi_retry);
  }
#endif

#ifndef __USE_WIFI_MANAGER__
  if (WiFi.SSID()) {
    if (wifi_connection_in_progress != true) {
      if (WiFi.getMode() == WIFI_AP) {
        if (logging) Serial.println(WiFi.SSID());
      } else {
        if (strlen(THINX_ENV_SSID) > 2) {
          WiFi.begin(THINX_ENV_SSID, THINX_ENV_PASS);
        }
        wifi_connection_in_progress = true; // prevents re-entering connect_wifi(); should timeout
      }
    }
  }
#endif

  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true; // prevents re-entering start() [this method]
    wifi_connection_in_progress = false;
  } else {
    connect_wifi();
    wifi_connection_in_progress = true;
  }
}

/*
* Connection to WiFi, called from connect() [if SSID & connected]
*/

void THiNX::connect_wifi() {

  #ifdef __USE_WIFI_MANAGER__
  return;
  #else

  if (wifi_connected) {
    return;
  }

  if (strlen(THINX_ENV_SSID) == 0) {
    Serial.println(F("THINX_ENV_SSID not set!"));
    return;
  }

    if (strlen(THINX_ENV_PASS) == 0) {
      Serial.println(F("THINX_ENV_PASS not set!"));
    return;
  }

  if (wifi_connection_in_progress) {
    if (wifi_retry > 1000) {
      if (WiFi.getMode() == WIFI_STA) {
        if (logging) Serial.println(F("*TH: Starting AP with PASSWORD..."));
        WiFi.mode(WIFI_AP);
        WiFi.softAP(accessPointName.c_str(), accessPointPassword.c_str()); // setup the AP on channel 1, not hidden, and allow 8 clients
        wifi_retry = 0;
        wifi_connection_in_progress = false;
        wifi_connected = true;
        return;
      } else {
        if ((strlen(THINX_ENV_SSID) > 2) && (strlen(THINX_ENV_PASS) > 2)) {
          WiFi.mode(WIFI_STA);
          WiFi.begin(THINX_ENV_SSID, THINX_ENV_PASS);
          wifi_connection_in_progress = true; // prevents re-entering connect_wifi()
          wifi_retry = 0; // waiting for sta...
        }
      }

    } else {
      if (logging) Serial.print(F("*TH: WiFi retry")); Serial.println(wifi_retry); Serial.flush();
      wifi_retry++;
    }

  } else {
    if (strlen(THINX_ENV_SSID) > 2) {
      if (wifi_retry == 0) {
        // 1st run
        if (WiFi.getMode() != WIFI_STA) {
          WiFi.mode(WIFI_STA);
        } else {
          WiFi.begin(THINX_ENV_SSID, THINX_ENV_PASS);
          wifi_connection_in_progress = true; // prevents re-entering connect_wifi()
        }
      }
    }
  }
  #endif
}

/*
* Registration
*/

void THiNX::checkin() {
  if (mem_check() == false) {
    return;
  }
#ifdef DEBUG
  if (logging) {
    Serial.println(F("*TH: Contacting API"));
  }
#endif
  if(!wifi_connected) {
    return; // if (logging) Serial.println(F("*TH: Cannot checkin while not connected, exiting."));
  } else {
    generate_checkin_body(); // returns json_buffer buffer
    if (forceHTTP) {
      senddata(json_buffer); // HTTP fallback
    } else {
      send_data(json_buffer); // HTTPS
    }
    checkin_time = millis() + checkin_interval;
  }
}

/*
* Registration - JSON body constructor
*/

char* THiNX::generate_checkin_body() {

  DynamicJsonBuffer jsonBuffer(512);
  JsonObject& root = jsonBuffer.createObject();

  root["mac"] = thinx_mac();

  if (strlen(thinx_firmware_version) > 1) {
    root["firmware"] = thinx_firmware_version;
  } else {
    root["firmware"] = THINX_FIRMWARE_VERSION;
  }

  if (strlen(thinx_firmware_version_short) > 1) {
    root["version"] = thinx_firmware_version_short;
  }

  if (strlen(thx_commit_id) > 1) {
    root["commit"] = thx_commit_id;
  }

  if (strlen(thinx_owner) > 1) {
    root["owner"] = thinx_owner;
  }

  if (strlen(thinx_alias) > 1) {
    root["alias"] = thinx_alias;
  }

  if (strlen(thinx_udid) > 4) {
    root["udid"] = thinx_udid;
  }

  if (statusString.length() > 0) {
    root["status"] = statusString.c_str();
  }

  // Optional location data
  root["lat"] = String(latitude);
  root["lon"] = String(longitude);
  root["rssi"] = String(WiFi.RSSI());
  // root["snr"] = String(100 + WiFi.RSSI() / WiFi.RSSI()); // approximate only

  // Flag for THiNX CI
  #ifndef PLATFORMIO_IDE
  // THINX_PLATFORM is not overwritten by builder in Arduino IDE
  root["platform"] = "arduino";
  #else
  root["platform"] = strdup(THINX_PLATFORM);
  #endif

  DynamicJsonBuffer wrapperBuffer(512);
  JsonObject& wrapper = wrapperBuffer.createObject();
  wrapper["registration"] = root;
  wrapper.printTo(json_buffer);
#ifdef DEBUG
  if (logging) {
    Serial.print("json_buffer body length: ");
    Serial.println(strlen(json_buffer));
  }
#endif
  return json_buffer;
}


/*
* Registration - HTTPS POST
*/

void THiNX::senddata(String body) {

  if (http_client.connect(thinx_cloud_url, 7442)) {

    http_client.println(F("POST /device/register HTTP/1.1"));
    http_client.print(F("Host: ")); http_client.println(thinx_cloud_url);
    http_client.print(F("Authentication: ")); http_client.println(thinx_api_key);
    http_client.println(F("Accept: application/json")); // application/json
    http_client.println(F("Origin: device"));
    http_client.println(F("Content-Type: application/json"));
    http_client.println(F("User-Agent: THiNX-Client"));
    http_client.println(F("Connection: close"));
    http_client.print(F("Content-Length: "));
    http_client.println(body.length());
    http_client.println();
    http_client.println(body);

    fetchdata();

  } else {
    if (logging) Serial.println(F("*TH: API connection failed."));
    return;
  }
}

void THiNX::fetchdata() {

  //if (logging) Serial.println(F("*TH: Waiting for API response..."));

  char buf[512];
  int pos = 0;

  // Wait until client available or timeout...
  unsigned long time_out = millis() + 30000;
  //if (logging) Serial.println(F("*TH: Waiting for client..."));

  while(!http_client.available()){
    if (millis() > time_out) {
      if (logging) Serial.println(F("*TH: HTTP Client not available."));
      return;
    }
    yield();
  }

  // Read while connected
  bool headers_passed = false;
  while ( http_client.available() ) {
    String line = "    ";
    if (!headers_passed) {
        line = http_client.readStringUntil('\n');
        if (line.length() < 3) {
          headers_passed = true;
        }
    } else {
        buf[pos] = http_client.read();
        pos++;
    }
  }

#ifdef DEBUG
  if (logging) Serial.println();
#endif
  buf[pos] = '\0'; // add null termination for any case...
  http_client.stop(); // ??
#ifdef DEBUG
  if (pos == 0) {
    if (logging) Serial.printf("*TH: API Communication error, fix me now!\n");
  }
#endif
  parse(buf);

}

// should use generic stream client as parameter
void THiNX::fetch_data() {

  char buf[512];
  int pos = 0;

  // Wait until client available or timeout...
  unsigned long time_out = millis() + 30000;
  //if (logging) Serial.println(F("*TH: Waiting for client..."));

  while(!https_client.available()){
    if (millis() > time_out) {
      if (logging) Serial.println(F("*TH: HTTP Client not available."));
      return;
    }
    yield();
  }

  // Read while connected
  bool headers_passed = false;
  while ( https_client.available() ) {
    String line = "    ";
    if (!headers_passed) {
        line = https_client.readStringUntil('\n');
        if (line.length() < 3) {
          headers_passed = true;
        }
    } else {
        buf[pos] = https_client.read();
        pos++;
    }
  }

#ifdef DEBUG
  if (logging) Serial.println();
#endif
  buf[pos] = '\0'; // add null termination for any case...
  https_client.stop(); // ??
#ifdef DEBUG
  if (pos == 0) {
    if (logging) Serial.printf("*TH: API Communication error, fix me now!\n");
  }
#endif
  parse(buf);
}

/* Secure version */
void THiNX::send_data(String body) {

#ifdef DEBUG
  if (logging) Serial.println(F("Secure API checkin..."));
#endif

  // Validating certificate authority because THiNX fingerprint changes every 90 days.
  // Use own self-signed certificate if in doubt of security. This serves to public only.
  //BearSSL::X509List trust_anchors(thx_ca_cert);
  //https_client.setTrustAnchors(&trust_anchors);

  //https_client.setInsecure(); // does not validate anything, very dangerous!

  if (https_client.connect(thinx_cloud_url, 7443)) {

    if (logging) Serial.println(F("HTTPS Client connected."));

    https_client.println(F("POST /device/register HTTP/1.1"));
    https_client.print(F("Host: ")); https_client.println(thinx_cloud_url);
    https_client.print(F("Authentication: ")); https_client.println(thinx_api_key);
    https_client.println(F("Accept: application/json")); // application/json
    https_client.println(F("Origin: device"));
    https_client.println(F("Content-Type: application/json"));
    https_client.println(F("User-Agent: THiNX-Client"));
    https_client.println(F("Connection: close"));
    https_client.print(F("Content-Length: "));
    https_client.println(body.length());
    https_client.println();
    https_client.println(body);

    fetch_data();

  } else {
    if (logging) Serial.println(F("*TH: API connection failed."));
    return;
  }
}

/*
* Response Parser
*/

int strpos(char *hay, char *needle, int offset)
{
    char haystack[strlen(hay)];
    strncpy(haystack, hay+offset, strlen(hay)-offset);
    char *p = strstr(haystack, needle);
    if (p)
        return p - haystack+offset;
    return -1;
}

void THiNX::parse(const char * pload) {

  if (ESP.getFreeHeap() < strlen(pload)) { // should be at least 4K for SSL!
    if (logging) Serial.println(F("*TH: Not enough RAM to parse!"));
    ESP.restart();
    return;
  }

  String payload = String(pload);

  // TODO: Temporary, remove!
  Serial.println("Parsing response:");
  Serial.println(payload);
  // <--

  payload_type ptype = Unknown;

  int start_index = -1;
  int endIndex = payload.length();

  int reg_index = payload.indexOf("{\"registration\"");
  int upd_index = payload.indexOf("{\"FIRMWARE_UPDATE\"");
  int not_index = payload.indexOf("{\"notification\"");
  int cfg_index = payload.indexOf("{\"configuration\"");
  int undefined_owner = payload.indexOf("old_protocol_owner:-undefined-");

  if (upd_index > start_index) {
    start_index = upd_index;
    ptype = UPDATE;
  }

  if (reg_index > start_index) {
    start_index = reg_index;
    endIndex = payload.indexOf("}}") + 2;
    ptype = REGISTRATION;
  }

  if (not_index > start_index) {
    start_index = not_index;
    endIndex = payload.indexOf("}}") + 2; // is this still needed?
    ptype = NOTIFICATION;
  }

  if (cfg_index > start_index) {
    start_index = cfg_index;
    endIndex = payload.indexOf("}}") + 2; // is this still needed?
    ptype = CONFIGURATION;
  }

  if (ptype == Unknown) {
    return;
  }

  if (undefined_owner > start_index) {
#ifdef DEBUG
    if (logging) Serial.println(F("ERROR: Not authorized. Please copy your owner_id into thinx.h from RTM Console > User Profile."));
#endif
    return;
  }

  String body = payload.substring(start_index, endIndex);
  DynamicJsonBuffer jsonBuffer(512);
  JsonObject& root = jsonBuffer.parseObject(body.c_str());

  if ( !root.success() ) {
    if (logging) Serial.println(F("Failed parsing root node."));
    return;
  }

  switch (ptype) {

    case UPDATE: {

      JsonObject& update = root["registration"];
      if (logging) Serial.println(F("TODO: Parse update payload..."));

      String mac = update["mac"];
      String this_mac = String(thinx_mac());
      if (logging) Serial.println(String("mac: ") + mac);

#ifdef DEBUG
      if (!mac.equals(this_mac)) {
        if (logging) Serial.println(F("*TH: Warning: firmware is dedicated to device with different MAC."));
      }
#endif

      String udid = root["udid"];
      if ( udid.length() > 4 ) {
        thinx_udid = strdup(udid.c_str());
      }

      // Check current firmware based on commit id and store Updated state...
      String commit = update["commit"];
#ifdef DEBUG
      if (logging) Serial.println(String("commit: ") + commit);
#endif

      // Check current firmware based on version and store Updated state...
      String version = update["version"];
#ifdef DEBUG
      if (logging) Serial.println(String("version: ") + version);
#endif

      //if ((commit == thinx_commit_id) && (version == thinx_version_id)) { WHY?
      if (strlen(available_update_url) > 5) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: firmware has same thx_commit_id as current and update availability is stored. Firmware has been installed."));
#endif
        available_update_url = strdup("");
        notify_on_successful_update();
        return;
      } else {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: Info: firmware has same thx_commit_id as current and no update is available."));
#endif
      }

      save_device_info();

      // In case automatic updates are disabled,
      // we must ask user to commence firmware update.
      if (thinx_auto_update == false) {
        if (mqtt_client != nullptr) {
#ifdef DEBUG
          if (logging) Serial.println(F("*TH: Update availability notification..."));
#endif
          mqtt_client->publish(
            thinx_mqtt_channel().c_str(),
            F("{ title: \"Update Available\", body: \"There is an update available for this device. Do you want to install it now?\", type: \"actionable\", response_type: \"bool\" }")
          );
          mqtt_client->loop();
        }

      } else if (thinx_auto_update || thinx_forced_update){

#ifdef DEBUG
        if (logging) Serial.println(F("*TH: Starting update A..."));
#endif

        String type = update["type"];
        // if (logging) Serial.print(F("*TH: Payload type: ")); if (logging) Serial.println(type);

        String files = update["files"];

        String url = update["url"]; // may be OTT URL
        available_update_url = url.c_str();

        String ott = update["ott"];
        available_update_url = ott.c_str();

        /*
        String hash = update["hash"];
        if (hash.length() > 2) {
          if (logging) Serial.print(F("*TH: #")); if (logging) Serial.println(hash);
          expected_hash = strdup(hash.c_str());
        }

        String md5 = update["md5"];
        if (md5.length() > 2) {
          if (logging) Serial.print(F("*TH: #")); if (logging) Serial.println(md5);
          expected_md5 = strdup(md5.c_str());
        }
        */

        // if (logging) Serial.println(F("Saving device info before firmware update.")); Serial.flush();
        save_device_info();

        if (url) {

          mqtt_client->publish(
            mqtt_device_status_channel,
            F("{ \"status\" : \"update_started\" }")
          );
          mqtt_client->loop();

          url.replace("http://", "");
          url.replace("thinx.cloud:7442", "");

          deferred_update_url = url;
          return;
        }
        return;
      }

    } break;

    case NOTIFICATION: {

      // Currently, this is used for update only, can be extended with request_category or similar.
      JsonObject& notification = root["notification"];

      if ( !notification.success() ) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: Failed parsing notification node."));
#endif
        return;
      }

      String type = notification["response_type"];
      if ((type == "bool") || (type == "boolean")) {
        bool response = notification["response"];
        if (response == true) {
#ifdef DEBUG
          //if (logging) Serial.println(F("*TH: User allowed update using boolean."));
#endif
          if (strlen(available_update_url) > 4) {
            deferred_update_url = available_update_url;
            return;
          }
        } else {
#ifdef DEBUG
          //if (logging) Serial.println(F("*TH: User denied update using boolean."));
#endif
        }
      }

      if ((type == "string") || (type == "String")) {
        String response = notification["response"];
        if (response == "yes") {
#ifdef DEBUG
          // if (logging) Serial.println(F("*TH: User allowed update using string."));
#endif
          if (strlen(available_update_url) > 4) {
            deferred_update_url = available_update_url;
            return;
          }
        } else if (response == "no") {
#ifdef DEBUG
          // if (logging) Serial.println(F("*TH: User denied update using string."));
#endif
        }
      }

    } break;

    case REGISTRATION: {

      JsonObject& registration = root["registration"];

      if ( !registration.success() ) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: Failed parsing registration node."));
#endif
        return;
      }

      // bool success = registration["success"]; unused
      String status = registration["status"];

      if (status == "OK") {

        String alias = registration["alias"];
        if ( alias.length() > 1 ) {
          thinx_alias = strdup(alias.c_str());
        }

        String owner = registration["owner"];
        if ( owner.length() > 1 ) {
          thinx_owner = strdup(owner.c_str());
        }

        String udid = registration["udid"];
        if ( udid.length() > 4 ) {
          thinx_udid = strdup(udid.c_str());
        }

        if (registration.containsKey(F("auto_update"))) {
          thinx_auto_update = (bool)registration[F("auto_update")];
        }

        if (registration.containsKey(F("forced_update"))) {
          thinx_forced_update = (bool)registration[F("forced_update")];
        }

        if (registration.containsKey(F("timestamp"))) {
#ifdef DEBUG
          if (logging) Serial.print(F("*TH: Updating THiNX time (UTC+0): "));
#endif
          last_checkin_timestamp = (long)registration[F("timestamp")] + timezone_offset * 3600;
          last_checkin_millis = millis();
#ifdef DEBUG
          if (logging) Serial.print(thinx_time(NULL));
          if (logging) Serial.print(" ");
          if (logging) Serial.println(thinx_date(NULL));
#endif
        }

        save_device_info();

      } else if (status == "FIRMWARE_UPDATE") {

        // Warning, this branch may be deprecated!

        String udid = registration["udid"];
        if ( udid.length() > 4 ) {
          thinx_udid = strdup(udid.c_str());
        }

#ifdef DEBUG
        // if (logging) Serial.println(F("Saving device info for update."));
#endif
        save_device_info();

        // String mac = registration["mac"];
#ifdef DEBUG
        // if (logging) Serial.println(String("*TH: Update for MAC: ") + mac);
#endif
        // TODO: must be current or 'ANY'

        String commit = registration["commit"];
#ifdef DEBUG
        if (logging) Serial.println(String("commit: ") + commit);
        if (commit == thinx_commit_id) {
          if (logging) Serial.println(F("*TH: Info: new firmware has same thx_commit_id as current."));
        }
#endif

        String version = registration["version"];
#ifdef DEBUG
        if (logging) Serial.println(String(F("*TH: version: ")) + version);
#endif

        if (thinx_auto_update == false) {
#ifdef DEBUG
          if (logging) Serial.println(String(F("*TH: Skipping auto-update (disabled).")));
#endif
          return;
        }

        String update_url;

        String url = registration["url"];
        if (url.length() > 2) {
#ifdef DEBUG
          if (logging) Serial.println(F("*TH: Starting direct update..."));
#endif
          update_url = url;
        }

        String ott = registration["ott"];
        if (ott.length() > 2) {
#ifdef DEBUG
          if (logging) Serial.println(F("*TH: Starting OTT update..."));
#endif
          // update_url = "http://thinx.cloud:7442/device/firmware?ott="+ott;
          update_url = "/device/firmware?ott="+ott;
        }

        /* duplicate code!
        String hash = registration["hash"];
        if (hash.length() > 2) {
          if (logging) Serial.print(F("*TH: #")); if (logging) Serial.println(hash);
          expected_hash = strdup(hash.c_str());
        }

        String md5 = registration["md5"];
        if (md5.length() > 2) {
          if (logging) Serial.print(F("*TH: #")); if (logging) Serial.println(md5);
          expected_md5 = strdup(md5.c_str());
        }*/

#ifdef DEBUG
        if (logging) Serial.println(update_url);
#endif
        deferred_update_url = update_url;
        return;

      }

    } break;

    case CONFIGURATION: {

      JsonObject& configuration = root["configuration"];

      if ( !configuration.success() ) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: Failed parsing configuration node."));
#endif
        return;
      }


#ifdef __ENABLE_WIFI_MIGRATION__
      //
      // Built-in support for WiFi migration
      //

      const char *ssid = configuration["THINX_ENV_SSID"];
      const char *pass = configuration["THINX_ENV_PASS"];

      // password may be empty string
      if ((strlen(ssid) > 2) && (strlen(pass) > 0)) {
        WiFi.disconnect();
        WiFi.begin(ssid, pass);
        unsigned long timeout = millis() + 20000;
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: Attempting WiFi migration..."));
#endif
        while (WiFi.status() != WL_CONNECTED) {
          yield();
          if (millis() > timeout) break;
        }
        if (WiFi.status() != WL_CONNECTED) {
#ifdef DEBUG
          if (logging) Serial.println(F("*TH: WiFi migration failed."));
#endif
        } else {
#ifdef DEBUG
          if (logging) Serial.println(F("*TH: WiFi migration successful.")); // TODO: Notify using publish() to device status topic
#endif
        }
      }
#endif
      // Forward update body to the library user
      if (_config_callback != NULL) {
        _config_callback(body);
      }

    } break;

    default:
    break;
  }

}

/*
* MQTT channel names
*/

String THiNX::thinx_mqtt_channel() {
  sprintf(mqtt_device_channel, "/%s/%s", thinx_owner, thinx_udid);
  return String(mqtt_device_channel);
}

String THiNX::thinx_mqtt_channels() {
  sprintf(mqtt_device_channels, "/%s/%s/#", thinx_owner, thinx_udid);
  return String(mqtt_device_channels);
}

String THiNX::thinx_mqtt_status_channel() {
  sprintf(mqtt_device_status_channel, "/%s/%s/status", thinx_owner, thinx_udid);
  return String(mqtt_device_status_channel);
}

unsigned long THiNX::epoch() {
  unsigned long since_last_checkin = (millis() - last_checkin_millis) / 1000;
  return last_checkin_timestamp + since_last_checkin;
}

String THiNX::thinx_time(const char* optional_format) {

  char *format = strdup(time_format);
  if (optional_format != NULL) {
    format = strdup(optional_format);
  }

  long stamp = THiNX::epoch();
  struct tm lt;
  char res[32];
  (void) localtime_r(&stamp, &lt);
  if (strftime(res, sizeof(res), format, &lt) == 0) {
      //if (logging) Serial.println(F("cannot format supplied time into buffer"));
  }
  return String(res);
}

String THiNX::thinx_date(const char* optional_format) {

  char *format = strdup(date_format);
  if (optional_format != NULL) {
    format = strdup(optional_format);
  }

  long stamp = THiNX::epoch();
  struct tm lt;
  char res[32];
  (void) localtime_r(&stamp, &lt);
  if (strftime(res, sizeof(res), format, &lt) == 0) {
      //if (logging) Serial.println(F("cannot format supplied date into buffer"));
  }
  return String(res);
}

/*
* Sends a MQTT message on successful update (should be used after boot).
*/

void THiNX::notify_on_successful_update() {
  // Notify on reboot for update
  if (mqtt_client != nullptr) {
    mqtt_client->publish(
      mqtt_device_status_channel,
      F("{ title: \"Update Successful\", body: \"The device has been successfully updated.\", type: \"success\" }")
    );
    unsigned long tout = millis() + 5000;
    while (millis() < tout) {
      mqtt_client->loop();
      delay(10);
    }
    mqtt_client->disconnect();
    mqtt_client->loop();
    delay(10);
  } else {
    if (logging) Serial.println(F("*TH: Device updated but MQTT not active to notify. TODO: Store."));
  }
}

/*
* Sends a MQTT message to Device's status topic (/owner/udid/status)
*/

void THiNX::publish_status(const char *message, bool retain) {

  // Early exit
  if (mqtt_client == nullptr) {
    #ifdef DEBUG
        if (logging) Serial.println(F("*TH: MQTT not active while trying to publish retained status."));
    #endif
    return;
  }

  // Check if connected and reconnect
  if (!mqtt_client->connected()) {
    printStackHeap("thx-pre-reconnect");
    // Reconnection
    if (logging) Serial.println(F("*TH: reconnecting MQTT in publish_status..."));
    printStackHeap("thx-pre-start");
    start_mqtt();
    printStackHeap("thx-post-start");
    unsigned long reconnect_timeout = millis() + 10000;
    while (!mqtt_client->connected()) {
      delay(10);
      if (millis() > reconnect_timeout) {
        if (logging) Serial.println(F("*TH: Reconnecting time-out!"));
        break;
      }
    }
  }

  if (mqtt_client->connected()) {
    printStackHeap("thx-pre-publish-status(2)");
    Serial.println(message);
    mqtt_client->publish(mqtt_device_status_channel, (const uint8_t*)message, strlen(message), retain);
    printStackHeap("thx-publish-pre-loop");
    mqtt_client->loop();
    printStackHeap("thx-publish-post-loop");
  } else {
    if (logging) Serial.println(F("*TH: Sending failed, MQTT disconnected!"));
  }

  mqtt_client->loop(); // kicks the MQTT immediately
  delay(10);
}

/*
* Sends a MQTT message to the Device Channel (/owner/udid)
*/

void THiNX::publish(char * message, char * topic, bool retain)  {
  char channel[256] = {0};
  sprintf(channel, "%s/%s", mqtt_device_channel, topic);
  if (mqtt_client != nullptr) {
    mqtt_client->publish(mqtt_device_status_channel, (const uint8_t*)message, strlen(message), retain);
    mqtt_client->loop();
    delay(10);
  } else {
#ifdef DEBUG
    if (logging) Serial.println(F("*TH: MQTT not active while trying to publish message."));
#endif
  }
}

/*
* Starts the MQTT client and attach callback function forwarding payload to parser.
*/

void setLastWill(String nextWill) {
  THiNX::lastWill = nextWill;
}

void THiNX::setLastWill(String nextWill) {
  mqtt_client->disconnect();
  start_mqtt();
}

bool THiNX::start_mqtt() {

#ifdef DEBUG
  if (logging) Serial.println(F("*TH: start_mqtt()"));
#endif

  if (mqtt_client) {
    mqtt_connected = mqtt_client->connected();
    return mqtt_connected;
  }

  if (strlen(thinx_udid) < 4) {
#ifdef DEBUG
    if (logging) Serial.println(F("*TH: MQTT NO-UDID!")); Serial.flush();
#endif
    return false;
  }

  if (forceHTTP == true) {
#ifdef DEBUG
    if (logging) Serial.println(F("*TH: Initializing new MQTT client."));
#endif
    if (mqtt_client == nullptr) {
      mqtt_client = new PubSubClient(http_client, thinx_mqtt_url, THINX_MQTT_PORT);
    }
  } else {
    bool res = true; // https_client.setCACert(thx_ca_cert); // should be loadCACert from file
    if (res) { // result of SSL certificate setting ignored so far
#ifdef DEBUG
      if (logging) Serial.println(F("*TH: Initializing new MQTTS client."));
#endif
      mqtt_client = new PubSubClient(https_client, thinx_mqtt_url, THINX_MQTTS_PORT);
    } else {
#ifdef DEBUG
      if (logging) Serial.println(F("*TH: Failed to load root CA certificate for MQTT!"));
#endif
    }
  }

  if (strlen(thinx_api_key) < 5) {
#ifdef DEBUG
    if (logging) Serial.println(F("*TH: API Key not set, exiting."));
#endif
    return false;
  }

  if (mqtt_client->connect(MQTT::Connect(thinx_mac())
    .set_will(thinx_mqtt_status_channel().c_str(), lastWill.c_str())
    .set_auth(thinx_udid, thinx_api_key)
    .set_keepalive(60))) {

    mqtt_connected = true;
    performed_mqtt_checkin = true;

    mqtt_client->set_callback([this](const MQTT::Publish &pub){


      // Stream has been never tested so far...
      if (pub.has_stream()) {

        /*
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: MQTT Type: Stream..."));
#endif
        uint32_t startTime = millis();
        uint32_t size = pub.payload_len();
        if ( ESP.updateSketch(*pub.payload_stream(), size, true, false) ) {
          // Notify on reboot for update
          mqtt_client->publish(
            mqtt_device_status_channel,
            "{ \"status\" : \"rebooting\" }"
          );
          mqtt_client->disconnect();
          pub.payload_stream()->stop();
          if (logging) Serial.printf("Update Success: %lu\nRebooting...\n", millis() - startTime);
          ESP.restart();
        } else {
          if (logging) Serial.println(F("*TH: ESP MQTT Stream update failed..."));
          mqtt_client->publish(
            mqtt_device_status_channel,
            "{ \"status\" : \"mqtt_update_failed\" }"
          );
        }
        */

      } else {
        // if (logging) Serial.println(F("*TH: MQTT Message Incoming:"));
        // Allocates payload copy buffer to allow sending MQTT responses inside _mqtt_callback
        unsigned int length = pub.payload_string().length(); // or sizeof(pub.payload_string().c_str());
        byte* p = (byte*)malloc(length);
        memcpy(p, pub.payload_string().c_str(), length);
        //if (logging) Serial.println(pub.payload_string());
        parse((const char*)p);
        if (_mqtt_callback) {
            _mqtt_callback(p);
        }
        free(p);
      }
    }); // end-of-callback

    return true;

  } else {
    mqtt_connected = false;
#ifdef DEBUG
    if (logging) Serial.println(F("*TH: MQTT Not connected."));
#endif
    return false;
  }
}

/*
* Restores Device Info. Callers (private): init_with_api_key; save_device_info()
* Provides: alias, owner, update, udid, (apikey)
*/

void THiNX::restore_device_info() {

  // if (logging) Serial.println(F("*TH: Checking device info..."));

  #ifndef __USE_SPIFFS__

  int value;
  long buf_len = 512;
  long data_len = 0;

  // if (logging) Serial.println(F("*TH: Restoring configuration from EEPROM..."));

  for (long a = 0; a < buf_len; a++) {
    value = EEPROM.read(a);
    json_buffer[a] = char(value);

    // validate at least data start
    if (a == 0) {
      if (value != '{') {
        return; // Not a JSON, nothing to do...
      }
    }
    if (value == '{') {
      json_end++;
    }
    if (value == '}') {
      json_end--;
    }
    if (value == 0) {
      json_buffer[a] = char(value);
      data_len++;
      if (logging) Serial.print("*THC: "); if (logging) Serial.print(a); if (logging) Serial.println(F(" bytes read from EEPROM."));
      // Validate JSON
      break;
    } else {
      json_buffer[a] = char(value);
      data_len++;
    }
    // Serial.flush(); // to debug reading EEPROM bytes
  }

#ifdef DEBUG
  if (logging) Serial.print(F("Restored json_buffer: "));
  if (logging) Serial.println(json_buffer);
#endif

  // Validating bracket count
  if (json_end != 0) {
    //if (logging) Serial.println(F("*TH: JSON invalid... bailing out."));
    return;
  }

  #else
  if (!SPIFFS.exists("/thinx.cfg")) {
    //if (logging) Serial.println(F("*TH: No saved configuration."));
    return;
  }
  File f = SPIFFS.open("/thinx.cfg", "r");
  if (!f) {
    return;
  }
  if (f.size() == 0) {
#ifdef DEBUG
    if (logging) Serial.println(F("*TH: Remote configuration file empty..."));
#endif
    return;
  }

  f.readBytesUntil('\r', json_buffer, sizeof(json_buffer));
  #endif

  DynamicJsonBuffer jsonBuffer(512); // tightly enough to fit ott as well
  JsonObject& config = jsonBuffer.parseObject((char*)json_buffer); // must not be String!

  if (!config.success()) {
    if (logging) Serial.println(F("*TH: No JSON data to be parsed..."));
    if (logging) Serial.println(json_buffer);
    return;

  } else {

    for (JsonObject::iterator it=config.begin(); it!=config.end(); ++it) {
      String key = it->key;
      //Serial.println();
      String value = config[it->key];
      // if (logging) Serial.println(value);
      if (key == "owner") {
        thinx_owner = strdup(value.c_str());
      }
      if (key == "apikey") {
        thinx_api_key = strdup(value.c_str());
      }
      if (key == "udid") {
        thinx_udid = strdup(value.c_str());
      }
      if (key == "alias") {
        thinx_alias = strdup(value.c_str());
      }
      if (key == "ott") {
        available_update_url = strdup(value.c_str());
      }
    }

    #ifdef __USE_SPIFFS__
    f.close();
    #else
    #endif
  }
}

/*
* Stores mutable device data (alias, owner) retrieved from API
*/

void THiNX::save_device_info()
{
  if (ESP.getFreeHeap() < 512) {
    if (logging) Serial.println(F("Not enough memory to build JSON."));
    return;
  }

  DynamicJsonBuffer jsonBuffer(512);
  JsonObject& root = jsonBuffer.createObject();

  // Mandatories

  if (strlen(thinx_owner) > 1) {
    root["owner"] = strdup(thinx_owner); // allow owner change
  }

  if (strlen(thinx_api_key) > 1) {
    root["apikey"] = strdup(thinx_api_key); // allow dynamic API Key changes
  }

  if (strlen(thinx_udid) > 1) {
    root["udid"] = strdup(thinx_udid); // allow setting UDID, skip 0
  }

  // Optionals

  if (strlen(thinx_alias) > 1) {
    root["alias"] = strdup(thinx_alias);
  }

  if (strlen(available_update_url) > 1) {
    root["update"] = strdup(available_update_url); // allow update
    //if (logging) Serial.println(F("*TH: available_update_url..."));
  }

  #ifdef __USE_SPIFFS__
  File f = SPIFFS.open("/thinx.cfg", "w");
  if (f) {
    root.printTo(f);
    f.print('\r');
    f.println();
    f.close();
    //if (logging) Serial.println(F("*TH: Saved configuration to SPIFFS."));
  } else {
    if (logging) Serial.println(F("*TH: Saving configuration failed!"));
    delay(5000);
  }
  #else
  if (logging) Serial.println(F("*TH: Saving configuration to EEPROM: "));
  for (long addr = 0; addr < strlen((const char*)json_info); addr++) {
    uint8_t byte = json_info[addr];
    EEPROM.put(addr, json_info[addr]);
    if (byte == 0) break;
  }
  EEPROM.commit();
  #endif
}

/*
* Fills output buffer with persistent dconfiguration JSON.
*/

void THiNX::deviceInfo() {

  DynamicJsonBuffer jsonBuffer(512);
  JsonObject& root = jsonBuffer.createObject();

  // Mandatories

  if (strlen(thinx_owner) > 1) {
    root["owner"] = strdup(thinx_owner); // allow owner change
  }

  if (strlen(thinx_api_key) > 1) {
    root["apikey"] = strdup(thinx_api_key); // allow dynamic API Key changes
  }

  if (strlen(thinx_udid) > 1) {
    root["udid"] = strdup(thinx_udid); // allow setting UDID, skip 0
  }

  // Optionals

  if (strlen(thinx_alias) > 1) {
    root["alias"] = strdup(thinx_alias);
  }

  if (strlen(available_update_url) > 1) {
    root["update"] = strdup(available_update_url); // allow update
    if (logging) Serial.println(F("*TH: available_update_url..."));
  }

  #ifdef __USE_SPIFFS__
  File f = SPIFFS.open("/thinx.cfg", "w");
  if (f) {
    root.printTo(f);
    f.print('\r');
    f.println();
    f.close();
    if (logging) Serial.println(F("*TH: Saved configuration to SPIFFS."));
  } else {
    if (logging) Serial.println(F("*TH: Saving configuration failed!"));
    delay(5000);
  }
  #else
  if (logging) Serial.println(F("*TH: Saving configuration to EEPROM: "));
  for (long addr = 0; addr < strlen((const char*)json_buffer); addr++) {
    uint8_t byte = json_buffer[addr];
    EEPROM.put(addr, json_buffer[addr]);
    if (byte == 0) break;
  }
  EEPROM.commit();
  #endif
}

/*
* Updates
*/

// update_file(name, data)
// update_from_url(name, url)

void THiNX::update_and_reboot(String url) {

  url.replace("http://", "");
  url.replace("thinx.cloud:7442", "");

  if (logging) Serial.print("*TH: Update with URL: ");
  if (logging) Serial.println(url);

  // #define __USE_STREAM_UPDATER__ ; // Warning, this is MQTT-based streamed update!
  #ifdef __USE_STREAM_UPDATER__
  if (logging) Serial.println(F("*TH: Starting MQTT & reboot..."));
  uint32_t size = pub.payload_len();
  if (ESP.updateSketch(*pub.payload_stream(), size, true, false)) {
    if (logging) Serial.println(F("Clearing retained message."));
    mqtt_client->publish(MQTT::Publish(pub.topic(), "").set_retain());
    mqtt_client->disconnect();

    if (logging) Serial.printf("Update Success: %lu\nRebooting...\n", millis() - startTime);

    notify_on_successful_update();
  }

  ESP.restart();
  #else

  // TODO: Download the file and check expected_hash first...

  if (logging) Serial.println(F("*TH: Starting ESP8266 HTTP Update & reboot..."));
  t_httpUpdate_return ret;
  if (forceHTTP) {
    /*
#ifdef DEBUG
    if (logging) { Serial.println(F("*TH: using http client and port 7442 host: "));
      Serial.print(thinx_cloud_url);
      Serial.print(F("*TH: URI: "));
      Serial.println(url);
      Serial.print(F("*TH: free heap: "));
      Serial.println(ESP.getFreeHeap());
    }
#endif
*/
    ret = ESPhttpUpdate.update(thinx_cloud_url, 7442, url, "");
  } else {
    // if (logging) Serial.println(F("*TH: using https client on port 7443"));
    ret = ESPhttpUpdate.update(thinx_cloud_url, 7443, url.c_str());
  }

  switch(ret) {
    case HTTP_UPDATE_FAILED:
    if (logging) Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
    setDashboardStatus(ESPhttpUpdate.getLastErrorString());
    break;

    case HTTP_UPDATE_NO_UPDATES:
    if (logging) Serial.println(F("HTTP_UPDATE_NO_UPDATES"));
    break;

    case HTTP_UPDATE_OK:
    if (logging) Serial.println(F("HTTP_UPDATE_OK"));
    //Serial.println(F("Firmware update completed. Rebooting soon..."));
    notify_on_successful_update();
    Serial.flush();
    ESP.restart();
    break;
  }
  #endif
}

/*
* Imports all required build-time values from thinx.h
*/

void THiNX::import_build_time_constants() {

  // Only if not overridden by user
  if (strlen(thinx_api_key) < 4) {
    thinx_api_key = strdup(THINX_API_KEY);
  }

  if (strlen(THINX_UDID) > 2) {
    thinx_udid = strdup(THINX_UDID);
  } else {
    thinx_udid = strdup("");
  }

  // Use commit-id from thinx.h if not given by environment
  #ifdef THX_COMMIT_ID
  thinx_commit_id = strdup(thx_commit_id);
  #else
  thinx_commit_id = strdup(THINX_COMMIT_ID);
  #endif

  thinx_mqtt_url = strdup(THINX_MQTT_URL);
  thinx_cloud_url = strdup(THINX_CLOUD_URL);
  thinx_alias = strdup(THINX_ALIAS);
  thinx_owner = strdup(THINX_OWNER);
  thinx_mqtt_port = THINX_MQTT_PORT;
  thinx_api_port = THINX_API_PORT;
  thinx_auto_update = THINX_AUTO_UPDATE;
  thinx_forced_update = THINX_FORCED_UPDATE;
  thinx_firmware_version = strdup(THINX_FIRMWARE_VERSION);
  thinx_firmware_version_short = strdup(THINX_FIRMWARE_VERSION_SHORT);
  app_version = strdup(THINX_APP_VERSION);
}

/*
* Performs the SPIFFS check and format if needed.
*/

bool THiNX::fsck() {
#if defined(ESP8266)
  String realSize = String(ESP.getFlashChipRealSize());
  String ideSize = String(ESP.getFlashChipSize());
  bool flashCorrectlyConfigured = realSize.equals(ideSize);
#else
  bool flashCorrectlyConfigured = true; //?
#endif
  bool fileSystemReady = false;
  if(flashCorrectlyConfigured == true) {
    #if defined(ESP8266)
      fileSystemReady = SPIFFS.begin();
    #else
      fileSystemReady = SPIFFS.begin(true); // formatOnFail=true
    #endif
    if (!fileSystemReady) {
      if (logging) Serial.println(F("*TH: Formatting SPIFFS..."));
      fileSystemReady = SPIFFS.format();;
      if (logging) Serial.println(F("*TH: Format complete, rebooting...")); Serial.flush();
      ESP.restart();
      return false;
    }
  }  else {
    if (logging) Serial.print(F("*TH: Flash incorrectly configured, SPIFFS cannot start."));
#if defined(ESP8266)
    if (logging) Serial.println(ideSize + ", real size: " + realSize);
#endif
  }
  return fileSystemReady ? true : false;
}

#ifdef __USE_WIFI_MANAGER__
/*
* API key update event
*/

void THiNX::evt_save_api_key() {
  if (should_save_config) {
    if (strlen(thx_api_key) > 4) {
      thinx_api_key = thx_api_key;
      // if (logging) Serial.print(F("Saving thx_api_key from Captive Portal."));
    }
    if (strlen(thx_owner_key) > 4) {
      thinx_owner_key = thx_owner_key;
      // if (logging) Serial.print(F("Saving thx_owner_key from Captive Portal."));
    }
    // if (logging) Serial.println(F("Saving device info for API key.")); Serial.flush();
    save_device_info();
    should_save_config = false;
  }
}
#endif

/*
* Final callback setter
*/

void THiNX::setPushConfigCallback( void (*func)(String) ) {
  _config_callback = func;
}

void THiNX::setFinalizeCallback( void (*func)(void) ) {
  _finalize_callback = func;
}

void THiNX::setMQTTCallback( void (*func)(byte*) ) {
    _mqtt_callback = func;
}

void THiNX::setMQTTBroker(char * url, int port) {
    thinx_mqtt_url = url;
    if (logging) Serial.println(F("Port is ignored, defaults to 1883"));
}

void THiNX::finalize() {
  thinx_phase = COMPLETED;
  if (_finalize_callback) {
    _finalize_callback();
  } else {
    //if (logging) Serial.println(F("*TH: Checkin completed (no _finalize_callback)."));
  }
}

/* This is necessary for SSL/TLS and should replace THiNX timestamp */
void THiNX::sync_sntp() {
  // THiNX API returns timezone_offset in current DST, if applicable
  configTime(timezone_offset * 3600, 0, "0.europe.pool.ntp.org", "cz.pool.ntp.org");
  time_t now = time(nullptr);
  while (now < timezone_offset * 3600) {
    delay(500);
    if (logging) Serial.print(".");
    now = time(nullptr);
  }
  if (logging) Serial.println();
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  //if (logging) Serial.print(F("*TH: SNTP time: "));
  //if (logging) Serial.print(asctime(&timeinfo));
}

void THiNX::setLocation(double lat, double lon) {
  latitude = lat;
  longitude = lon;
  // uses mqtt_connected status because this happens only after first checkin
  // and thus prevents premature request to backend.
  if (wifi_connected && thinx_phase > FINALIZE) {
    //if (logging) Serial.println(F("*TH: LOOP » setLocation checkin"));
    checkin();
  }
}

void THiNX::setDashboardStatus(String newstatus) {
  statusString = newstatus;
  if (wifi_connected && thinx_phase > FINALIZE) {
    //if (logging) Serial.println(F("*TH: LOOP » setDashboardStatus checkin"));
    checkin();
    if (mqtt_client) {
      String message = String("{ \"status\" : \"") + newstatus + String("\" }");
      mqtt_client->publish(mqtt_device_status_channel, message.c_str());
    }
  }
}

// deprecated since 2.2 (3)
void THiNX::setStatus(String newstatus) {
  setDashboardStatus(newstatus);
}

void THiNX::setCheckinInterval(long interval) {
  checkin_interval = interval;
}

void THiNX::setRebootInterval(long interval) {
  reboot_interval = interval;
}

// SHA256

/* Calculates SHA-256 of a file
bool THiNX::check_hash(char * filename, char * expected) {
  File file = SPIFFS.open(filename, "r");
  char aes_text[2 * SHA256_BLOCK_SIZE + 1];

  static uint8_t buf[512] = {0};
  static uint8_t obuf[512] = {0};
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  uint32_t fpos = 0;

  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    Sha256 *sha256Instance = new Sha256();

    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      sha256Instance->update((const unsigned char*)buf, toRead);
      fpos += toRead;
      len -= toRead;
    }

    sha256Instance->final(obuf);
    delete sha256Instance;

    for (int i = 0; i < SHA256_BLOCK_SIZE; ++i) {
      sprintf(aes_text + 2 * i, "%02X", obuf[i]);
    }

    //if (logging) Serial.printf("AES # %s at %u\n", aes_text, fpos);
    //if (logging) Serial.printf("EXPECTED # %s", expected);

    end = millis() - start;
    //if (logging) Serial.printf("%u bytes hashed in %u ms\n\n", flen, end);

    file.close();

    return strcmp(expected, aes_text);

  } else {
    if (logging) Serial.println(F("Failed to open file for reading"));
    return false;
  }
}
*/

// Prepared for refactoring loop sections out to keep less stack movement

void THiNX::do_connect_wifi() {

}

void THiNX::do_mqtt_connect() {

}

void THiNX::do_mqtt_checkin() {

}

void THiNX::do_connect_api() {

}

void THiNX::do_deferred_update() {

}

/*
* Core loop
*/

void THiNX::loop() {

  //printStackHeap("in");

  if (thinx_phase == CONNECT_WIFI) {
    // If not connected manually or using WiFiManager, start connection in progress...
    if (WiFi.status() != WL_CONNECTED) {
      wifi_connected = false;
      if (wifi_connection_in_progress != true) {
        if (logging) Serial.println(F("*TH: CONNECTING »"));
        connect(); // blocking
        wifi_connection_in_progress = true;
        wifi_connection_in_progress = true;
        return;
      } else {
        return;
      }
    } else {
      wifi_connected = true;

      // Synchronize SNTP time
      //if (logging) Serial.println(""); // newline after "Time difference for DST"
      sync_sntp();

      // Start MDNS broadcast
#ifdef __DISABLE_PROXY__
#else
      if (!MDNS.begin(thinx_alias)) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: Error setting up mDNS"));
#endif
      } else {
        // Query MDNS proxy
        //if (logging) Serial.println(F("*TH: Searching for thinx-connect on local network..."));
        int n = MDNS.queryService("thinx", "tcp"); // TODO: WARNING! may be _tcp!
        if (n > 0) {
          thinx_cloud_url = strdup(String(MDNS.hostname(0)).c_str());
          thinx_mqtt_url = strdup(String(MDNS.hostname(0)).c_str());
        }
      }
#endif

      thinx_phase = CONNECT_API;
      return;
    }
  }

  // After MQTT gets connected:
  if (thinx_phase == CHECKIN_MQTT) {
    thinx_mqtt_channel(); // initialize channel variable
    if (strlen(mqtt_device_channel) > 5) {
      if (mqtt_client->subscribe(mqtt_device_channel)) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: MQTT connected."));
#endif
        // Publish status on status topic
        mqtt_client->publish(
          mqtt_device_status_channel,
          generate_checkin_body() // F("{ \"status\" : \"connected\" }")
        );
        mqtt_client->loop();
        delay(10);
        thinx_phase = FINALIZE;
        return;
      }
    }
  }

  if ( thinx_phase == CONNECT_MQTT ) {
    if (strlen(thinx_udid) > 4) {
      if (mqtt_connected == false) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: PHASE » CONNECT_MQTT"));
#endif
        mqtt_connected = start_mqtt();
        mqtt_client->loop();
        delay(10);
        if (mqtt_connected) {
#ifdef DEBUG
            if (logging) Serial.println(F("*TH: PHASE » CHECKIN_MQTT"));
#endif
            thinx_phase = CHECKIN_MQTT;
        } else {
          // tries again next time
        }
        return;
      } else {
        thinx_phase = FINALIZE;
        return;
      }
    } else {
#ifdef DEBUG
      if (logging) Serial.println(F("*TH: LOOP » FINALIZE"));
#endif
      thinx_phase = FINALIZE;
      return;
    }
  }

  /*
  if (thinx_phase > CHECKIN_MQTT) {
    if (logging) Serial.println(F("*TH: start_mqtt")); Serial.flush();
    mqtt_connected = mqtt_client->connected();
    if (!mqtt_connected) {
      if (logging) Serial.println(F("*TH: MQTT RECONNECT ON CONNECTION CHECK..."));
      if (logging) Serial.println(thinx_time(NULL));
      mqtt_connected = start_mqtt();
      if (mqtt_connected) {
          thinx_phase = CHECKIN_MQTT;
      } else {
        // tries again next time
      }
    }
  }*/

  // CASE thinx_phase == CONNECT_API

  // Force re-checkin after specified interval
  if (thinx_phase > FINALIZE) {
    if (millis() > checkin_time) {
      if (checkin_interval > 0) {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: LOOP » Checkin interval arrived..."));
#endif
        thinx_phase = CONNECT_API;
        checkin_interval = millis() + checkin_time;
      }
    }
  }

  // If connected, perform the MQTT loop and bail out ASAP
  if (thinx_phase == CONNECT_API) {
    if (WiFi.getMode() == WIFI_AP) {
#ifdef DEBUG
      if (logging) Serial.println(F("*TH: LOOP « (AP_MODE)"));
#endif
      return;
    }
    if (strlen(thinx_api_key) > 4) {
      checkin(); // warning, this blocking and takes time, thus return...
      if (mqtt_connected == false) {
        thinx_phase = CONNECT_MQTT;
      } else {
#ifdef DEBUG
        if (logging) Serial.println(F("*TH: LOOP » FINALIZE (mqtt connected)"));
#endif
        thinx_phase = FINALIZE;
      }
    }
  }

  if ( thinx_phase == FINALIZE ) {
    finalize();
  }

  if ( thinx_phase >= FINALIZE ) {
    if (mqtt_client) {
      mqtt_client->loop();
      delay(10);
    }
  }

  // deferred_update_url is set by response parser
  if (deferred_update_url.length() > 0) {
    if (ESP.getFreeHeap() > 3000) {
#ifdef DEBUG
      if (logging) Serial.println(F("*TH: Starting deferred firmware update..."));
#endif
      update_and_reboot(deferred_update_url);
    } else {
#ifdef DEBUG
      if (logging) Serial.println(F("*TH: Deferring firmware update..."));
#endif
    }
  }

  if ( (reboot_interval > 0) && (millis() > reboot_interval) ) {
    setDashboardStatus(F("Rebooting..."));
    ESP.restart();
  }

  #ifdef __USE_WIFI_MANAGER__
    // Save API key on change
    if (should_save_config) {
      //if (logging) Serial.println(F("*TH: Saving API key on change..."));
      evt_save_api_key();
      should_save_config = false;
    }
  #endif

  //printStackHeap("out");
}

// ESP8266/ESP32 Specific Implementations

/*
* Device MAC address
*/

#ifdef ESP32
const char * THiNX::thinx_mac() {
  sprintf(mac_string, "5CCF%6X", ESP.getEfuseMac());
  return mac_string;
}
#endif

#ifdef ESP8266
const char * THiNX::thinx_mac() {
  sprintf(mac_string, "5CCF7F%6X", ESP.getChipId());
  return mac_string;
}
#endif

bool THiNX::mem_check() {
  if (ESP.getFreeHeap() < 3000) { // should be at least 4K for SSL!
#ifdef DEBUG
    if (logging) Serial.println(F("*TH: Not enough RAM to checkin!"));
#endif
    return false;
  }
  return true;
}

#endif // IMPORTANT LINE FOR UNIT-TESTING!
