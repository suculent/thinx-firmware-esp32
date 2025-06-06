#pragma GCC diagnostic warning "-Wdeprecated-declarations"

extern "C"
{
#include "user_interface.h"
#include "thinx.h"
#include <cont.h>
#include <time.h>
#include <stdlib.h>
  extern cont_t g_cont;
}

#include "THiNXLib32.h"

const int API_KEY_TLEN = 64;
#define OWNER_KEY_TLEN API_KEY_TLEN

#ifndef strdup()
// Reimplementation just to fix missing in THiNX32 since Arduino Core 3.0
char *strdup(const char *source)
{
  int len = strlen(source);
  char *dest = new char[len];
  for (int i = 0; i < len; i++)
    dest[i] = source[i];

  return dest;
}
#endif

#ifndef THINX_FIRMWARE_VERSION_SHORT
#define THINX_FIRMWARE_VERSION_SHORT VERSION
#endif

#define MQTT_RECONNECT_DELAY 60000 // ms; time after which broken MQTT connection will trigger reboot instead of reconnect

#ifndef THINX_COMMIT_ID
// any commit ID is sufficient to allow update
#define THINX_COMMIT_ID "0c48a9ab0c4f89c4b8fb72173553d3e74986632d0"
#endif

// Static variables
char *THiNX::thinx_api_key;
char *THiNX::thinx_owner_key;
char THiNX::json_buffer[768];

const char THiNX::time_format[] = "%T";
const char THiNX::date_format[] = "%Y-%m-%d";
char *THiNX::thinx_mqtt_url = (char*)THINX_MQTT_URL;
char *THiNX::thinx_cloud_url = (char*)THINX_CLOUD_URL;
double THiNX::benchmark_speed = 0.0f; // kilobytes per second

#include "thinx_root_ca.h"

#ifdef __USE_WIFI_MANAGER__
char THiNX::thx_api_key[65] = {0};
char THiNX::thx_owner_key[65] = {0};
int THiNX::should_save_config = 0;

WiFiManagerParameter *THiNX::api_key_param;
WiFiManagerParameter *THiNX::owner_param;

void THiNX::saveConfigCallback()
{
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println(F("* TH: WiFiManager configuration should be saved now!"));
  should_save_config = true;
  strncpy(thx_api_key, api_key_param->getValue(), api_key_param->getValueLength());
  strncpy(thx_owner_key, owner_param->getValue(), owner_param->getValueLength());
}
#endif

double THiNX::latitude = 0.0;
double THiNX::longitude = 0.0;
String THiNX::statusString = "Registered";
String THiNX::accessPointName = "THiNX-AP";
String THiNX::accessPointPassword = "PASSWORD";
String THiNX::lastWill = "{ \"status\" : \"disconnected\" }";

bool THiNX::logging = false;

// #define LOG `if (logging)`

uint32_t THiNX::last_free_heap_size;

/* Convenience method for debugging memory issues. */
void THiNX::printStackHeap(const String &tag)
{
  if (!logging)
    return;
  uint32_t heap = ESP.getFreeHeap();
  uint32_t diff = 0;
  String way = "=";
  // if (last_free_heap_size != heap) { // rate limiting
  if (last_free_heap_size > heap)
  {
    diff = last_free_heap_size - heap;
    way = "-";
  }
  else
  {
    diff = heap - last_free_heap_size;
    way = "+";
  }
  if (diff < 1)
    return;
#ifdef DEBUG
  if (logging)
  {
    Serial.print(tag);
    Serial.print(" HEAP: ");
    Serial.print(heap);
    Serial.print("; ");
    Serial.print(way);
    Serial.println(diff);
  }
#endif
  last_free_heap_size = heap;
  //}
}

/* Constructor */

THiNX::THiNX()
{
}

/* Designated Initializers */

THiNX::THiNX(const char *__apikey)
{

  THiNX(__apikey, "");
}

THiNX::THiNX(const char *__apikey, const char *__owner_id)
{

#ifdef DEBUG_RAM
  if (logging)
    printStackHeap("before-init");
#endif

  //Serial.print("Init with owner:"); Serial.println(__owner_id);

  thinx_phase = INIT;

#ifdef __USE_WIFI_MANAGER__
  should_save_config = false;
  WiFiManager wifiManager;
  api_key_param = new WiFiManagerParameter("apikey", "API Key", thinx_api_key, 64);
  wifiManager.addParameter(api_key_param);
  owner_param = new WiFiManagerParameter("owner", "Owner ID", thinx_owner_key, 64);
  wifiManager.addParameter(owner_param);
#ifdef DEBUG
  wifiManager.setTimeout(30);
#else
  wifiManager.setTimeout(60);
#endif
  wifiManager.setDebugOutput(true);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  std::vector<const char *> wm_menu  = {"wifi", "info", "exit"}; // sep, update
  wifiManager.setShowInfoUpdate(false);
  wifiManager.setShowInfoErase(false);
  wifiManager.setMenu(wm_menu);

  wifiManager.autoConnect(accessPointName.c_str());

  // TODO: Go to sleep if not connected in 5 minutes...
#endif

  json_buffer[0] = 0;

  if (once != true)
  {
    once = true;
  }

  status = WL_IDLE_STATUS;
  wifi_connected = false;
  mqtt_client = nullptr;
  mqtt_connected = false;
  performed_mqtt_checkin = false;
  wifi_connection_in_progress = false;
  wifi_retry = 0;

  app_version = (char*)"\0";
  available_update_url = (char*)"\0";

  thinx_firmware_version_short = (char*)"\0";
  thinx_firmware_version = (char*)"\0";
  env_hash = ENV_HASH;
  thinx_version_id = (char*)"\0";
  thinx_api_key = (char*)"\0";
  thinx_forced_update = false;
  last_checkin_timestamp = 0; // 1/1/1970
  mqtt_reconnect_timeout = 0;

  checkin_time = millis() + checkin_interval / 4; // retry faster before first checkin
  reboot_interval = millis() + reboot_timeout;

  deferred_update_url = ""; // may be loaded from device info or set from registration

  // will be loaded from SPIFFS/EEPROM or retrieved on Registration later
  if (strlen(__owner_id) < 1)
  {
    thinx_owner = (char*)THINX_OWNER;
    Serial.print("Overriding thinx_owner with:"); Serial.println(thinx_owner);
  }

  EEPROM.begin(512); // should be SPI_FLASH_SEC_SIZE

  import_build_time_constants();

  restore_device_info();

  info_loaded = true;

#ifdef __USE_WIFI_MANAGER__
  wifi_connected = true;
#else
  if ((WiFi.status() == WL_CONNECTED) && (WiFi.getMode() == WIFI_STA))
  {
    wifi_connected = true;
    wifi_connection_in_progress = false;
  }
  else
  {
    WiFi.mode(WIFI_STA);
  }
#endif

  bool api_key_valid = false;

  if (strlen(__apikey) > 4)
  {
    thinx_api_key = (char*)__apikey;
    api_key_valid = true;
  }
  else
  {
    return;
  }

  bool owner_valid = false;

  if (strlen(__owner_id) == OWNER_KEY_TLEN)
  {
    thinx_owner = (char*)__owner_id;
    owner_valid = true;
  }
  else
  {
    if (logging) Serial.print(F("*TH: No Owner ID!"));
    thinx_owner = (char*)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    Serial.print("Set zero"); Serial.println(thinx_owner);
    return;
  }

  if (api_key_valid && owner_valid) {
    init_thinx_mqtt_channel();
    init_with_api_key(thinx_api_key);
  }

  wifi_connection_in_progress = false; // last
}

// Designated initializer
void THiNX::init_with_api_key(const char *__apikey)
{

#ifdef __USE_SPIFFS__
  // if (logging) Serial.println(F("*TH: Checking filesystem, please wait..."));
  if (!fsck())
  {
    if (logging)
      Serial.println(F("*TH: Filesystem check failed, disabling THiNX."));
    return;
  }
#endif

  if (info_loaded == false)
  {
    restore_device_info(); // loads saved apikey/ownerid
    info_loaded = true;
  }

  if (strlen(__apikey) > 4)
  {
    thinx_api_key = (char*)__apikey;
  }
  else
  {
    if (strlen(thinx_api_key) < 4)
    {
#ifdef DEBUG
      if (logging)
        Serial.print(F("*TH: No API Key!"));
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

char *THiNX::get_udid()
{
  return (char*)thinx_udid;
}

void THiNX::connect()
{

  if (wifi_connected)
  {
    return;
  }

#ifdef DEBUG
  if (logging)
  {
    Serial.print(F("*TH: connecting: "));
    Serial.println(wifi_retry);
  }
#endif

  if (wifi_conection_timeout > 0 && (millis() > wifi_conection_timeout))
  {
    wifi_connection_in_progress = false;
  }

#ifndef __USE_WIFI_MANAGER__
  if (WiFi.SSID())
  {
    if (wifi_connection_in_progress != true)
    {
      if (WiFi.getMode() == WIFI_AP)
      {
        if (logging)
        {
          Serial.print("AP: ");
          Serial.println(WiFi.SSID());
        }
      }
      else
      {
        if (strlen(THINX_ENV_SSID) > 2)
        {
          WiFi.begin(THINX_ENV_SSID, THINX_ENV_PASS);
          wifi_conection_timeout = millis() + 30000; // TODO: NICE-TO-HAVE: implement linear or exponential backoff
        }
        wifi_connection_in_progress = true; // prevents re-entering connect_wifi(); reset after wifi_conection_timeout
      }
    }
  }
#endif

  if (WiFi.status() == WL_CONNECTED)
  {
    wifi_connected = true; // prevents re-entering start() [this method]
    wifi_connection_in_progress = false;
  }
  else
  {
    connect_wifi();
    wifi_connection_in_progress = true;
  }
}

/*
 * Connection to WiFi, called from connect() [if SSID & connected]
 */

void THiNX::connect_wifi()
{

#ifdef __USE_WIFI_MANAGER__
  return;
#else

  if (wifi_connected)
  {
#ifdef DEBUG
    Serial.println(F("wifi_connected == true"));
#endif
    return;
  }

  if (wifi_connection_in_progress)
  {

    if (wifi_retry > 1)
    {
      if (WiFi.getMode() == WIFI_STA)
      {
        if (logging)
          Serial.println(F("*TH: Starting AP with PASSWORD..."));
        WiFi.mode(WIFI_AP);
        WiFi.softAP(accessPointName.c_str(), accessPointPassword.c_str()); // setup the AP on channel 1, not hidden, and allow 8 clients
        wifi_retry = 0;
        wifi_connection_in_progress = false;
        wifi_connected = true;
        return;
      }
      else
      {
        if (strlen(THINX_ENV_SSID) > 2)
        {
          WiFi.mode(WIFI_STA);
          WiFi.begin(THINX_ENV_SSID, THINX_ENV_PASS);
          wifi_connection_in_progress = true; // prevents re-entering connect_wifi()
          wifi_retry = 0;                     // waiting for sta...
        }
      }
    }
    else
    {
      if (logging)
        Serial.print(F("*TH: WiFi retry #"));
      Serial.println(wifi_retry);
      Serial.flush();
      wifi_retry++;
      wifi_connection_in_progress = false; // ignore wifi
    }
  }
  else
  {
    if (strlen(THINX_ENV_SSID) > 2)
    {
      if (wifi_retry == 0)
      {
        // 1st run
        if (WiFi.getMode() != WIFI_STA)
        {
          WiFi.mode(WIFI_STA);
        }
        else
        {
          WiFi.begin(THINX_ENV_SSID, THINX_ENV_PASS);
          wifi_connection_in_progress = true; // prevents re-entering connect_wifi() until timeout
        }
      }
    }
  }
#endif
}

/*
 * Registration
 */

void THiNX::checkin()
{
  if (!mem_check())
    return;
  /* not guaranteed here
#ifdef DEBUG
  if (logging) {
    Serial.println(F("*TH: THiNX API checkin..."));
  }
#endif
*/
  if (!wifi_connected)
    return; // if (logging) Serial.println(F("*TH: Cannot checkin while not connected, exiting."));

  generate_checkin_body(); // returns json_buffer buffer
#ifndef __DISABLE_HTTPS__
  send_data_secure(json_buffer); // HTTPS
#else
  send_data(json_buffer); // HTTP fallback
#endif

  checkin_time = millis() + checkin_interval;
}

/*
 * Registration - JSON body constructor
 */

void THiNX::generate_checkin_body()
{

  DynamicJsonDocument root(512);

  root["registration"]["mac"] = thinx_mac();

#ifdef DEBUG
    //Serial.print("Reporting thinx_mac: "); Serial.println(thinx_mac());
#endif

  if (strlen(thinx_firmware_version_short) > 1)
  {
    root["registration"]["version"] = String(thinx_firmware_version_short);
#ifdef DEBUG
    //Serial.print("Reporting thinx_firmware_version_short: "); Serial.println(thinx_firmware_version_short);
#endif
  }

  if ((thinx_owner != nullptr) && (strlen(thinx_owner) == OWNER_KEY_TLEN))
  {
    root["registration"]["owner"] = thinx_owner;
#ifdef DEBUG
    //Serial.print("Reporting thinx_owner: "); Serial.println(thinx_owner);
#endif
  } else {
    Serial.print("Bad owner:"); Serial.println(thinx_owner);
  }

  if (strlen(thinx_alias) > 1)
  {
    root["registration"]["alias"] = thinx_alias;
#ifdef DEBUG
    //Serial.print("Reporting thinx_alias: "); Serial.println(thinx_alias);
#endif
  }

  if (strlen(thinx_udid) > 4)
  {
    root["registration"]["udid"] = thinx_udid;
#ifdef DEBUG
    Serial.print("Reporting UDID: "); Serial.println(thinx_udid);
#endif
  }

  if (strlen(thinx_firmware_version) > 1)
  {
#ifdef DEBUG
    //Serial.print("Reporting thinx_firmware_version: "); Serial.println(thinx_firmware_version);
#endif
    root["registration"]["firmware"] = String(thinx_firmware_version);
#ifdef DEBUG
    //Serial.print("Reported thinx_firmware_version: "); Serial.println(thinx_firmware_version);
#endif
  }
  else
  {
#ifdef DEBUG
    //Serial.print("Reporting embedded firmware ID: "); Serial.println(THINX_FIRMWARE_VERSION);
#endif
    root["registration"]["firmware"] = String(THINX_FIRMWARE_VERSION);
#ifdef DEBUG
    //Serial.println("Reported embedded firmware ID.");
#endif
  }

/*
#ifdef DEBUG
    Serial.print("Reporting env_hash: "); Serial.println(ENV_HASH);
#endif
  if (strlen(env_hash) > 1)
  {
    root["registration"]["env_hash"] = String(ENV_HASH);
#ifdef DEBUG
    Serial.println("Reported env_hash.");
#endif
  }
*/

#ifdef DEBUG
    //Serial.print("Reporting statusString: "); Serial.println(statusString);
#endif
  if (statusString.length() > 0)
  {
    root["registration"]["status"] = statusString;
#ifdef DEBUG
    //Serial.println("Reported statusString"); 
#endif
  }

  // Optional location data
  //root["registration"]["lat"] = String(latitude);
  //root["registration"]["lon"] = String(longitude);
  //root["registration"]["rssi"] = String(WiFi.RSSI());
  // root["snr"] = String(100 + WiFi.RSSI() / WiFi.RSSI()); // approximate only

  char platform_temp[24] = {0};
  char *mcu_type = (char*)"unknown";

#ifdef ESP32
  mcu_type = (char*)"esp32";
#endif
#ifdef ESP8266
  mcu_type = (char*)"esp8266";
#endif

// Flag for THiNX CI
#ifndef PLATFORMIO_IDE
  // THINX_PLATFORM is not overwritten by builder in Arduino IDE
  // root["platform"] = "arduino";
  snprintf(platform_temp, sizeof(platform_temp), "%s:%s", "arduino", mcu_type);
#else
  // root["platform"] = strdup(THINX_PLATFORM);
  snprintf(platform_temp, sizeof(platform_temp), "%s:%s", THINX_PLATFORM, mcu_type);
#endif

  // since v2.7 (breaking change)
#ifdef DEBUG
    //Serial.print("Reporting platform_temp: "); Serial.println(platform_temp);
#endif
  root["registration"]["platform"] = platform_temp;

  // since 2.8.240 (DevSec)
  char flash_chip_id[13] = {0};
#ifdef ESP8266
  snprintf(flash_chip_id, sizeof(flash_chip_id), "%.6X", ESP.getFlashChipId());
#endif
#ifdef ESP32
  uint64_t macAddress = ESP.getEfuseMac();
  uint64_t macAddressTrunc = macAddress << 40;
  uint32_t chipID = macAddressTrunc >> 40;
  snprintf(flash_chip_id, sizeof(flash_chip_id), "5CCF7F%.6X", chipID);
#endif

  root["registration"]["fcid"] = flash_chip_id;

#ifdef DEBUG
  if (logging)
    serializeJson(root, Serial);
  if (logging)
    Serial.println();
#endif
  serializeJson(root, json_buffer);
}

/*
 * Registration - HTTP POST
 */

#ifdef __DISABLE_HTTPS__
void THiNX::send_data(const String &body)
{
  if (!http_client.connect(thinx_cloud_url, 7442))
  {
    if (logging)
      Serial.println(F("*TH: API connection failed."));
    return;
  }

  http_client.println(F("POST /device/register HTTP/1.1"));
  http_client.print(F("Host: "));
  http_client.println(THINX_CLOUD_URL);
  http_client.print(F("Authentication: "));
  http_client.println(THINX_API_KEY);
  http_client.println(F("Accept: application/json")); // application/json
  http_client.println(F("Origin: device"));
  http_client.println(F("Content-Type: application/json"));
  http_client.println(F("User-Agent: THiNX-Client"));
  http_client.println(F("Connection: close"));
  http_client.print(F("Content-Length: "));
  http_client.println(body.length());
  http_client.println();
  http_client.println(body);

  //Serial.print("*TH: [httpclient body] ");
  //Serial.println(body);

  fetch_data(&http_client);
}
#endif

#ifdef __DISABLE_HTTPS__
void THiNX::fetch_data(WiFiClient *client)
{

  unsigned long benchmark_start = 0;

  char buf[768];
  int pos = 0;
  unsigned long time_out = millis() + 30000;

  while (!client->available())
  {
    if (millis() > time_out)
    {
#ifdef DEBUG
      if (logging)
        Serial.println(F("*TH: HTTP request timeout."));
#endif
      return;
    }
    yield();
  }

  int bytes = 0;

  // Read while connected
  bool headers_passed = false;
  benchmark_start = millis();
  while (client->available())
  {
    String line = "    ";
    if (!headers_passed)
    {
      line = client->readStringUntil('\n');
      bytes += line.length();
      if (line.length() < 3)
      {
        headers_passed = true;
      }
    }
    else
    {
      buf[pos] = client->read();
      bytes++;
      pos++;
    }
  }

  buf[pos] = '\0'; // add null termination for any case...
  client->stop();  // ??

  unsigned long benchmark_time = millis() - benchmark_start;
#ifdef DEBUG
  Serial.print(F("Fetched "));
  Serial.print(bytes);
  Serial.print(F(" bytes in "));
  Serial.print(benchmark_time);
  Serial.println(" ms");
  //Serial.println(String(buf));
#endif
  double in_second = double(1000) / double(benchmark_time);
  benchmark_speed = double(bytes * in_second / 1000);
#ifdef DEBUG
  Serial.print(F("Benchmark speed is "));
  Serial.print(benchmark_speed);
  Serial.println(F(" kb/s"));
#endif

#ifdef DEBUG
  if (pos == 0)
  {
#ifdef DEBUG
    if (logging)
      Serial.printf("*TH: API Communication error, fix me now!\n");
#endif
  }
#endif
  parse(buf);
}
#endif

#ifndef __DISABLE_HTTPS__
void THiNX::fetch_data_secure(BearSSL::WiFiClientSecure *client)
{
  // if (logging) Serial.println(F("*TH: Waiting for API response..."));

  char buf[768];
  int pos = 0;

  unsigned long interval = 30000;
  unsigned long currentMillis = millis(), previousMillis = millis();

  // Wait until client available or timeout...
  unsigned long time_out = millis() + 30000;
  // if (logging) Serial.println(F("*TH: Waiting for client..."));

  while (!client->available())
  {
    if (millis() > time_out)
    {
      if (logging)
        Serial.println(F("*TH: Client NOT available."));
      return;
    }
    yield();
  }

  // Read while connected
  bool headers_passed = false;
  while (client->available())
  {
    String line = "    ";
    if (!headers_passed)
    {
      line = client->readStringUntil('\n');
      yield();
      if (line.length() < 3)
      {
        headers_passed = true;
      }
    }
    else
    {
      buf[pos] = client->read();
      yield();
      pos++;
    }
  }

#ifdef DEBUG
  // if (logging) Serial.println();
#endif
  buf[pos] = '\0'; // add null termination for any case...
  client->stop();  // ??
#ifdef DEBUG
  // if (logging) Serial.println();
  if (pos == 0)
  {
    if (logging)
      Serial.printf("*TH: API Communication error, fix me now!\n");
  }
#endif
  parse(buf);
}
#endif

#ifndef __DISABLE_HTTPS__
/* Secure version */
void THiNX::send_data_secure(const String &body)
{

  int ret = ESP.getFreeHeap();

#ifdef DEBUG
  if (logging)
    Serial.println(F("Secure API checkin..."));
#endif

  https_client.setInsecure(); // does not validate anything, very dangerous!

  bool mfln = https_client.probeMaxFragmentLength(thinx_cloud_url, 7443, 512);
#ifdef DEBUG
  if (logging)
    Serial.printf("MFLN supported: %s\n", mfln ? "yes" : "no");
#endif
  if (mfln)
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("Setting MFLN buffer sizes to 512"));
#endif
    https_client.setBufferSizes(512, 512);
  }

  if (!https_client.connect(thinx_cloud_url, 7443))
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: API connection failed."));
#endif
    return;
  }
  else
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("HTTPS Client connected."));
#endif
  }

#ifdef DEBUG
  if (logging)
    Serial.printf("MFLN status: %s\n", https_client.getMFLNStatus() ? "true" : "false");
  if (logging)
    Serial.printf("Memory used: %d\n", ret - ESP.getFreeHeap());
#endif

  https_client.println(F("POST /device/register HTTP/1.1"));
  https_client.print(F("Host: "));
  https_client.println(thinx_cloud_url);
  https_client.print(F("Authentication: "));
  https_client.println(thinx_api_key);
  https_client.println(F("Accept: application/json")); // application/json
  https_client.println(F("Origin: device"));
  https_client.println(F("Content-Type: application/json"));
  https_client.println(F("User-Agent: THiNX-Client"));
  https_client.print(F("Content-Length: "));
  https_client.println(body.length());
  https_client.println();
  https_client.println(body);

  fetch_data_secure(&https_client);
}
#endif

/*
 * Response Parser
 */

int strpos(char *hay, char *needle, int offset)
{
  char haystack[strlen(hay)];
  strncpy(haystack, hay + offset, strlen(hay) - offset);
  char *p = strstr(haystack, needle);
  if (p)
    return p - haystack + offset;
  return -1;
}

void THiNX::parse(const char *pload)
{
  if (ESP.getFreeHeap() < strlen(pload))
  { // should be at least 4K for SSL!
    if (logging)
      Serial.println(F("*TH: Not enough RAM to parse!"));
    Serial.flush();
    delay(5000);
    ESP.restart();
    return;
  }

  payload_type ptype = Unknown;

  int start_index = 0;

  int32_t reg_index = (int32)strstr(pload, "\"registration");
  int32_t upd_index = (int32)strstr(pload, "\"FIRMWARE_UPDATE");
  int32_t not_index = (int32)strstr(pload, "\"notification");
  int32_t cfg_index = (int32)strstr(pload, "\"configuration");

  if (upd_index > start_index)
  {
    start_index = upd_index;
    ptype = UPDATE;
  }

  if (reg_index > start_index)
  {
    start_index = reg_index;
    ptype = REGISTRATION;
  }

  if (not_index > start_index)
  {
    start_index = not_index;
    ptype = NOTIFICATION;
  }

  if (cfg_index > start_index)
  {
    start_index = cfg_index;
    ptype = CONFIGURATION;
  }

  if (ptype == Unknown)
  {
#ifdef DEBUG
    Serial.println(F("*THD: unknown ptype"));
#endif
    return;
  }

  DynamicJsonDocument root(768); // hopefully this is enough, otherwise 200 bytes leak somewhere here

  auto error = deserializeJson(root, pload);

  if (error)
  {
    Serial.print(F("*THD: Failed parsing root node!"));
    Serial.println(error.c_str());
#ifdef DEBUG
    Serial.println(pload);
#endif
    return;
  }

  switch (ptype)
  {

  case UPDATE:
  {

    JsonObject update = root["registration"];

    String alias = update["alias"];
    if (alias.length() > 4)
    {
      thinx_alias = strdup(alias.c_str());
    }

   String udid = update["udid"];
   if (strlen(udid.c_str()) <= 37)
   {
     thinx_udid = strdup(udid.c_str());
     Serial.print("UDID: ");
     Serial.println(thinx_udid);
   }
   else
   {
     Serial.print("Invalid UDID length: "); Serial.print(strlen(udid.c_str())); Serial.print(" ");
     Serial.println(udid);
   }

    if (update.containsKey(F("auto_update")))
    {
      thinx_auto_update = (bool)update[F("auto_update")];
    }

#ifdef DEBUG
    String mac = update["mac"];
    mac.replace(":", "");
    if (!mac.equals(String(thinx_mac())))
    {
      if (logging)
      {
        Serial.println(F("*TH: Warning: firmware MAC does not match."));
        //Serial.println(String("mac: ") + mac);
        //Serial.println("In pload:");
        //Serial.println(pload);
      }
    }
    else
    {
      //Serial.println("MAC OK");
    }
#endif

    // Check current firmware based on version and store Updated state...
    String version = update["version"];
    String envh = update["env_hash"];
#ifdef DEBUG
    if (logging)
      Serial.println(String("version: ") + version);
#endif

    if (version.indexOf(String(VERSION)) == 0)
    {
#ifdef DEBUG
      if (logging)
        Serial.println(F("FIXME version is equal..."));
#endif
      if (envh.length() > 0)
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("FIXME env hash exists..."));
#endif
        if (envh.indexOf(String(ENV_HASH)) != -1)
        {
#ifdef DEBUG
          if (logging)
            Serial.println(F("FIXME env is same..."));
#endif
            // env is set and same
#ifdef DEBUG
          if (logging)
            Serial.println(F("*TH: firmware has same version and env. Firmware has been already installed."));
#endif
          available_update_url = "";
          notify_on_successful_update();
          return;
        }
        else
        {
          // env is set but different
          if (logging)
            Serial.println(F("FIXME env is different, should update..."));
#ifdef DEBUG
          if (logging)
            Serial.println(F("*TH: new firmware has different env."));
#endif
        }
      }
      else
      {
        // env is not set, version is same
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: firmware has same version and no env. Firmware has been already installed."));
#endif
        available_update_url = "";
        notify_on_successful_update();
        return;
      }
    }

#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: proceeding to update..."));
#endif

    save_device_info(); // warning, seems to cause 216 bytes memory leak!

    // In case automatic updates are disabled,
    // we must ask user to commence firmware update.
    if (thinx_auto_update == false)
    {
      Serial.println("auto_update disabled");
      if (mqtt_client != nullptr)
      {
        /*
#ifdef DEBUG
          if (logging) Serial.println(F("*TH: Update availability notification..."));
#endif
          mqtt_client->publish(
            mqtt_device_channel,
            F("{ title: \"Update Available\", body: \"There is an update available for this device. Do you want to install it now?\", type: \"actionable\", response_type: \"bool\" }")
          );
          mqtt_client->loop();
          */
      }
    }

    if (thinx_auto_update || thinx_forced_update)
    {

      String update_url;

      // legacy, overridden
      String url = update["url"]; // deprecated
      if (url.length() > 4)
      {
        update_url = url;
      }

      // preferred
      String ott = update["ott"];
      if (ott.length() > 4)
      {
        update_url = "/device/firmware?ott=" + ott;
      }

      if (update_url.length() > 4)
      {
        update_url.replace("http://", "");
        update_url.replace(":7442", "");
        update_url.replace(":7443", "");
        update_url.replace(thinx_cloud_url, "");
        deferred_update_url = String(update_url); // needs a copy because string will not exist later
        available_update_url = deferred_update_url.c_str();
        return;
      }
      return;
    }
  }
  break;

  case NOTIFICATION:
  {

    Serial.println("notification...");

    // Currently, this is used for update only, can be extended with request_category or similar.
    JsonObject notification = root["notification"];

    if (notification.isNull())
    {
#ifdef DEBUG
      if (logging)
        Serial.println(F("*TH: Failed parsing notification node."));
#endif
      return;
    }

    String type = notification["response_type"];
    if ((type == "bool") || (type == "boolean"))
    {
      bool response = notification["response"];
      if (response == true)
      {
#ifdef DEBUG
        // if (logging) Serial.println(F("*TH: User allowed update using boolean."));
#endif
        if (strlen(available_update_url) > 4)
        {
          deferred_update_url = String(available_update_url);
          return;
        }
      }
      else
      {
#ifdef DEBUG
        // if (logging) Serial.println(F("*TH: User denied update using boolean."));
#endif
      }
    }

    if ((type == "string") || (type == "String"))
    {
      String response = notification["response"];
      if (response == "yes")
      {
#ifdef DEBUG
        // if (logging) Serial.println(F("*TH: User allowed update using string."));
#endif
        if (strlen(available_update_url) > 4)
        {
          deferred_update_url = String(available_update_url);
          return;
        }
      }
      else if (response == "no")
      {
#ifdef DEBUG
        // if (logging) Serial.println(F("*TH: User denied update using string."));
#endif
      }
    }
  }
  break;

  case REGISTRATION:
  {

    JsonObject registration = root["registration"];

    if (registration.isNull())
    {
#ifdef DEBUG
      if (logging)
        Serial.println(F("*TH: Failed parsing registration node."));
#endif
      return;
    }

    // bool success = registration["success"]; unused
    String status = registration["status"];

    if (status == "OK")
    {

      String alias = registration["alias"];
      if (alias.length() > 1 && (alias.indexOf("null") != 0))
      {
        thinx_alias = strdup(alias.c_str());
      }

      String owner = registration["owner"];
      if (owner.length() > 4)
      {
        strcpy(thinx_owner, owner.c_str());
      }

      String udid = registration["udid"];
      //Serial.print("Loaded UDID: "); Serial.println(udid);
      const char *udid_s = udid.c_str();
      if (strlen(udid_s) == 36)
      {
        strcpy(thinx_udid, udid_s);
      } else {
        Serial.print("Skipped UDID length = "); Serial.println(strlen(udid.c_str()));
      }

      if (registration.containsKey(F("auto_update")))
      {
        thinx_auto_update = (bool)registration[F("auto_update")];
      }

      if (registration.containsKey(F("forced_update")))
      {
        thinx_forced_update = (bool)registration[F("forced_update")];
      }

      if (registration.containsKey(F("timestamp")))
      {
#ifdef DEBUG
        if (logging)
          Serial.print(F("*TH: Updating THiNX time (UTC+0): "));
#endif
        last_checkin_timestamp = (unsigned long)registration[F("timestamp")] /*  + timezone_offset * 3600 */;
        last_checkin_millis = millis();
      }

      save_device_info();
    }
    else if (status == "FIRMWARE_UPDATE")
    {

      // Warning, this branch may be deprecated!

      const char* udid = registration["udid"];
      if (strlen(udid) > 4)
      {
        thinx_udid = strdup(udid);
      }

#ifdef DEBUG
      // if (logging) Serial.println(F("Saving device info for update."));
#endif
      save_device_info();

      // String mac = registration["mac"];
#ifdef DEBUG
      // if (logging) Serial.println(String("*TH: Update for MAC: ") + mac);
#endif
      // DEPRECATED:
      // String commit = registration["commit"];

      String version = registration["version"];
#ifdef DEBUG
      if (logging)
        Serial.println(String(F("*TH: version: ")) + version);
      if (logging)
        Serial.println(String(F("*TH: current app_version: ")) + app_version);
#endif
      if (version.indexOf(String(app_version)) != -1)
      {
        Serial.print("app_version: ");
        Serial.println(app_version);
        Serial.print("thinx_firmware_version_short: ");
        Serial.println(thinx_firmware_version_short);
        Serial.print("thinx_firmware_version: ");
        Serial.println(thinx_firmware_version);
        Serial.println("Not new version (!), skipping update.");
        return;
      }

      if (thinx_auto_update == false)
      {
#ifdef DEBUG
        if (logging)
          Serial.println(String(F("*TH: Skipping auto-update (disabled).")));
#endif
        return;
      }

      bool forwardUpdateURL = false;

      String update_url;

      String url = registration["url"];
      if (url.length() > 5)
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: Starting direct update..."));
#endif
        update_url = url;
        forwardUpdateURL = true;
      }

      String ott = registration["ott"];
      if (ott.length() > 5)
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: Starting OTT update..."));
#endif
        // update_url = "http://thinx.cloud:7442/device/firmware?ott="+ott;
        update_url = "/device/firmware?ott=" + ott;
        forwardUpdateURL = true;
      }

      if (!forwardUpdateURL)
      {
        Serial.println(F("*TH: Forward URL not given, rebooting for classic OTA update."));
        Serial.println(F("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-\n"));
        Serial.flush();
        delay(1000);
        ESP.restart();
      }
      else
      {
        if (logging)
          Serial.println(F("*TH: Using Forward URL/OTT for deferred immediate update."));
        deferred_update_url = String(update_url);
        return;
      }
    }
  }
  break;

  case CONFIGURATION:
  {

    JsonObject configuration = root["configuration"];

    if (configuration.isNull())
    {
#ifdef DEBUG
      if (logging)
        Serial.println(F("*TH: Failed parsing configuration node."));
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
    if ((strlen(ssid) > 2) && (strlen(pass) > 0))
    {
      WiFi.disconnect();
      WiFi.begin(ssid, pass);
      unsigned long timeout = millis() + 20000;
#ifdef DEBUG
      if (logging)
        Serial.println(F("*TH: Attempting WiFi migration..."));
#endif
      while (WiFi.status() != WL_CONNECTED)
      {
        yield();
        if (millis() > timeout)
          break;
      }
      if (WiFi.status() != WL_CONNECTED)
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: WiFi migration failed."));
#endif
      }
      else
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: WiFi migration successful.")); // TODO: Notify using publish() to device status topic
#endif
      }
    }
#endif

    // Forward update body to the library user, impacts stack with pload again!
    if (_config_callback != NULL)
    {
      _config_callback((char *)pload);
    }
  }
  break;

  default:
    break;
  }
}

/*
 * MQTT channel names
 */

void THiNX::init_thinx_mqtt_channel()
{
  snprintf(mqtt_device_channel, sizeof(mqtt_device_channel), "/%s/%s", thinx_owner, thinx_udid);
}

String THiNX::thinx_mqtt_channels()
{
  snprintf(mqtt_device_channels, sizeof(mqtt_device_channels), "/%s/%s/#", thinx_owner, thinx_udid);
  return String(mqtt_device_channels);
}

char *THiNX::generate_mqtt_status_channel()
{
  snprintf(mqtt_device_status_channel, sizeof(mqtt_device_status_channel), "/%s/%s/status", thinx_owner, thinx_udid);
  return mqtt_device_status_channel;
}

unsigned long THiNX::epoch()
{
  unsigned long since_last_checkin = (millis() - last_checkin_millis) / 1000;
  return last_checkin_timestamp + since_last_checkin;
}

/*
 * Sends a MQTT message on successful update (should be used after boot).
 */

void THiNX::notify_on_successful_update()
{
  // Notify on reboot for update
  if (mqtt_client != nullptr)
  {
    mqtt_client->publish(
        mqtt_device_status_channel,
        F("{ title: \"Update Successful\", body: \"The device has been successfully updated.\", type: \"success\" }"));
    unsigned long tout = millis() + 5000;
    while (millis() < tout)
    {
      mqtt_client->loop();
      delay(10);
    }
    mqtt_client->disconnect();
    mqtt_client->loop();
    delay(10);
  }
  else
  {
    if (logging)
      Serial.println(F("*TH: Device updated but MQTT not active to notify. TODO: Store."));
  }
}

/*
 * Sends a MQTT message to Device's status topic (/owner/udid/status)
 */

void THiNX::publish_status_unretained(const char *message)
{
  // Early exit
  if ((mqtt_client == nullptr) || (mqtt_client == NULL))
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT not active while trying to publish retained status."));
#endif
    return;
  }

  publish_status(message, false);
  if (mqtt_client)
  {
    mqtt_client->loop(); // kicks the MQTT immediately (if any)
    delay(10);
  }
}

void THiNX::publish_status(const char *message, bool retain)
{

  // Early exit
  if (mqtt_client == nullptr)
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT not active while trying to publish retained status."));
#endif
    return;
  }

#ifdef DEBUG
  // if (logging) Serial.print("*TH > "); Serial.println(message);
#endif

  if (mqtt_client->connected())
  {
    mqtt_client->publish(mqtt_device_status_channel, (const uint8_t *)message, strlen(message), retain);
    mqtt_client->loop();
  }
  else
  {

#ifdef DEBUG
    // printStackHeap("thx-pre-publish-status(R2-not-connected!)");
    // Reconnection
    if (logging)
      Serial.println(F("*TH: reconnecting MQTT in publish_status..."));
      // printStackHeap("thx-pre-start");
#endif
    mqtt_client = nullptr;
    start_mqtt();
    if (mqtt_client == nullptr)
    {
      Serial.println(F("*TH: MQTT init failed!"));
      return;
    }
#ifdef DEBUG
    // printStackHeap("thx-post-start");
#endif
    unsigned long reconnect_timeout = millis() + 10000;
    while (!mqtt_client->connected())
    {
      delay(10);
      if (millis() > reconnect_timeout)
      {
        break;
      }
    }

    if (!mqtt_client->connected())
    {
      // enable timeout if none
      if (mqtt_reconnect_timeout == 0)
      {
        mqtt_reconnect_timeout = millis() + MQTT_RECONNECT_DELAY;
      }
      if (millis() > mqtt_reconnect_timeout)
      {
        if (logging)
          Serial.println(F("*TH: NOT Rebooting, but MQTT reconnect failed..."));
        //Serial.flush();
        //ESP.restart();
      }
      return; // early exit: no publish/loop until reconnected
    }
    else
    {
      mqtt_reconnect_timeout = 0;
    }

    mqtt_client->publish(mqtt_device_status_channel, (const uint8_t *)message, strlen(message), retain);
    mqtt_client->loop();
  }
}

/*
 * Sends a MQTT message to the Device Channel (/owner/udid)
 */

// Old version, leaks strings, deprecated.
void THiNX::publish(const String &message, const String &topic, bool retain)
{
  String channel = String(mqtt_device_channel) + String("/") + String(topic);
  if (mqtt_client != nullptr)
  {
    if (retain == true)
    {
      mqtt_client->publish(
          MQTT::Publish(channel.c_str(), message.c_str()).set_retain());
    }
    else
    {
      mqtt_client->publish(channel.c_str(), message.c_str());
    }
    mqtt_client->loop();
    delay(10);
  }
  else
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT not active while trying to publish message."));
#endif
  }
}

void THiNX::publish(char *message, char *topic, bool retain)
{
  char channel[256] = {0};
  snprintf(channel, sizeof(channel), "%s/%s", mqtt_device_channel, topic);
  if (mqtt_client != nullptr)
  {
    if (retain == true)
    {
      mqtt_client->publish(
          MQTT::Publish(channel, message).set_retain());
    }
    else
    {
      mqtt_client->publish(channel, message);
    }
    mqtt_client->loop();
    delay(10);
  }
  else
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT not active while trying to publish message."));
#endif
  }
}

/*
 * Starts the MQTT client and attach callback function forwarding payload to parser.
 */

void setLastWill(const String &nextWill)
{
  THiNX::lastWill = nextWill;
}

void THiNX::setLastWill(const String &nextWill)
{
  mqtt_client->disconnect();
  start_mqtt();
}

bool THiNX::start_mqtt()
{

#ifdef DEBUG
  if (logging)
    //Serial.println(F("*TH: start_mqtt()"));
#endif

  if (mqtt_client != nullptr)
  {

#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT client non-null, checking connected state..."));
#endif

    mqtt_connected = mqtt_client->connected();
    if (mqtt_connected)
    {
      return true;
    }
    // ignore non-working MQTT for now (EAV does not use that much)
    finalize();
    return true;
  }

  if (strlen(thinx_udid) < 4)
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT NO-UDID!"));
    Serial.flush();
#endif
    return false;
  }

#ifdef DEBUG
  if (logging)
    Serial.println(F("*TH: Initializing new MQTTS client."));
#endif

#ifndef __DISABLE_HTTPS__
  mqtt_client = new PubSubClient(https_client, thinx_mqtt_url, 8883);
#else
  mqtt_client = new PubSubClient(http_client, thinx_mqtt_url);
#endif

  if (strlen(thinx_api_key) < 5)
  {
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: API Key not set, exiting."));
#endif
    return false;
  }

  generate_mqtt_status_channel();

  /*
  #ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT->connect()"));
    if (logging)
      Serial.print(F("*TH: UDID: "));
    if (logging)
      Serial.println(thinx_udid);
    if (logging)
      Serial.print(F("*TH: AK: "));
    if (logging)
      Serial.println(thinx_api_key);
  #endif
  */

  if (mqtt_client->connect(MQTT::Connect(thinx_mac())
                               .set_will(mqtt_device_status_channel, lastWill.c_str())
                               .set_auth(thinx_udid, thinx_api_key)
                               .set_keepalive(45)))
  {

    mqtt_connected = true;
    performed_mqtt_checkin = true;

    mqtt_client->set_callback([this](const MQTT::Publish &pub)
                              {
      // Stream has been never tested so far...
      if (pub.has_stream())
      {

        //#ifdef DEBUG
        Serial.println(F("*TH: MQTT Type: Stream..."));
        Serial.flush();
        //#endif
        if (_update_callback != nullptr)
        {
          _update_callback();
        }

        uint32_t startTime = millis();
        uint32_t size = pub.payload_len();
        if (ESP.updateSketch(*pub.payload_stream(), size, true, false))
        {
          // Notify on reboot for update
          mqtt_client->publish(
              mqtt_device_status_channel,
              "{ \"status\" : \"rebooting\" }");
          mqtt_client->disconnect();
          pub.payload_stream()->stop();
          if (logging)
            Serial.printf("Update Success: %lu\nRebooting...\n", millis() - startTime);
          Serial.flush();
          ESP.restart();
        }
        else
        {
          if (logging)
            Serial.println(F("*TH: ESP MQTT Stream update failed..."));
          mqtt_client->publish(
              mqtt_device_status_channel,
              "{ \"status\" : \"mqtt_update_failed\" }");
        }
      }
      else
      {
        if (logging)
        {
          //Serial.print(F("Payload string: "));
          //Serial.println(pub.payload_string());
        }
        parse(pub.payload_string().c_str());
        if (_mqtt_callback)
        {
          _mqtt_callback((byte *)pub.payload_string().c_str());
        }
      } }); // end-of-callback

    return true;
  }
  else
  {
    mqtt_connected = false;
#ifdef DEBUG
    if (logging)
      Serial.println(F("*TH: MQTT Not connected."));
#endif
    return false;
  }
}

/*
 * Restores Device Info. Callers (private): init_with_api_key; save_device_info()
 * Provides: alias, owner, update, udid, (apikey)
 */

void THiNX::restore_device_info()
{

  // if (logging) Serial.println(F("*TH: Checking device info..."));

#ifndef __USE_SPIFFS__

  int value;
  int json_end = 0;
  long buf_len = 512;
  long data_len = 0;

  // if (logging) Serial.println(F("*TH: Restoring configuration from EEPROM..."));

  for (long a = 0; a < buf_len; a++)
  {
    value = EEPROM.read(a);
    json_buffer[a] = char(value);

    // validate at least data start
    if (a == 0)
    {
      if (value != '{')
      {
        return; // Not a JSON, nothing to do...
      }
    }
    if (value == '{')
    {
      json_end++;
    }
    if (value == '}')
    {
      json_end--;
    }
    if (value == 0)
    {
      json_buffer[a] = char(value);
      data_len++;
      if (logging)
        Serial.print("*THC: ");
      if (logging)
        Serial.print(a);
      if (logging)
        Serial.println(F(" bytes read from EEPROM."));
      // Validate JSON
      break;
    }
    else
    {
      json_buffer[a] = char(value);
      data_len++;
    }
    // Serial.flush(); // to debug reading EEPROM bytes
  }

#ifdef DEBUG
  Serial.print(F("Restored json_buffer: "));
  Serial.println(json_buffer);
#endif

  // Validating bracket count
  if (json_end != 0)
  {
    // if (logging) Serial.println(F("*TH: JSON invalid... bailing out."));
    return;
  }

#else
  if (!SPIFFS.exists("/thinx.cfg"))
  {
    // if (logging) Serial.println(F("*TH: No saved configuration."));
    return;
  }
  File f = SPIFFS.open("/thinx.cfg", "r");
  if (!f)
  {
    return;
  }
  if (f.size() == 0)
  {
#ifdef DEBUG
    Serial.println(F("*TH: Remote configuration file empty..."));
#endif
    return;
  }

  f.readBytesUntil('\r', json_buffer, sizeof(json_buffer));
#endif

  DynamicJsonDocument config_doc(512); // tightly enough to fit ott as well
  auto error = deserializeJson(config_doc, (char *)json_buffer);

  if (error)
  {
    Serial.println(F("*TH: No device info JSON data to be parsed..."));
    if (logging)
      Serial.println(json_buffer);
    return;
  }
  else
  {

    // JsonObject config = config_doc.as<JsonObject>();

    const char *owner = config_doc["owner"];
    if (owner)
    {
      strcpy(thinx_owner, owner);
      Serial.print("Set strdup(owner)"); Serial.println(thinx_owner);
    }

    const char *apikey = config_doc["apikey"];
    if (apikey)
    {
      thinx_api_key = strdup(apikey);
    }

    const char *udid = config_doc["udid"];
    if (udid)
    {
      thinx_udid = strdup(udid);
    }

    const char *alias = config_doc["alias"];
    if (alias)
    {
      //thinx_alias = strdup(alias);
    }

    const char *ott = config_doc["ott"];
    if (ott)
    {
      available_update_url = strdup(ott);
    }

    // Serial.println(F("debugging device info:"));
    // Serial.printl("o: "); Serial.println(thinx_owner);
    // Serial.printl("k: "); Serial.println(thinx_api_key);
    // Serial.println("udid: "); Serial.println(thinx_udid);
    // Serial.println("alias: "); Serial.println(thinx_alias);

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
  if (ESP.getFreeHeap() < 512)
  {
    if (logging)
      Serial.println(F("Not enough memory to build JSON."));
    return;
  }

  DynamicJsonDocument root(512); // seems too long and wrongly named, may cause ambiguation

  // Mandatories

  if ((thinx_owner != nullptr) && (strlen(thinx_owner) == OWNER_KEY_TLEN))
  {
    root["owner"] = thinx_owner; // allow owner change
  } else {
    Serial.println("Invalid OWNER_KEY_TLEN!");
  }

  if (strlen(thinx_api_key) > 1)
  {
    root["apikey"] = thinx_api_key; // allow dynamic API Key changes
  }

  if (strlen(thinx_udid) > 1)
  {
    root["udid"] = thinx_udid; // allow setting UDID, skip 0
    //Serial.print("UDID TLEN: "); Serial.println(strlen(thinx_udid));
  }

  // Optionals

  if (strlen(thinx_alias) > 1)
  {
    root["alias"] = thinx_alias;
  }

  if (strlen(available_update_url) > 1)
  {
    root["update"] = available_update_url; // stores data for forced OTT update on reboot
  }

#ifdef __USE_SPIFFS__

  File f = SPIFFS.open("/thinx.cfg", "w");
  if (f)
  {
    serializeJson(root, f);

#ifdef DEBUG
    // Serial.println("Serialized json for length reservation: '");
    // serializeJson(root, Serial);
    // Serial.println("'");
#endif

    f.print('\r');
    f.println();
    f.close();
  }
  else
  {
    if (logging)
      Serial.println(F("*TH: Saving configuration failed!"));
    delay(3000);
  }

#else

  uint8_t json_info[512] = {};

  serializeJson(root, json_info);

  if (logging)
    Serial.println(F("*TH: Saving configuration to EEPROM: "));
  for (long addr = 0; addr < strlen((const char *)json_info); addr++)
  {
    uint8_t byte = json_info[addr];
    EEPROM.put(addr, json_info[addr]);
    if (byte == 0)
      break;
  }
  EEPROM.commit();

#endif
}

/*
 * Updates
 */

// update_file(name, data)
// update_from_url(name, url)

void THiNX::update_and_reboot(String url)
{

  if (url.length() < 5)
  {
    Serial.print("Invalid URL:");
    Serial.println(url);
    return;
  }

#ifdef DEBUG
  Serial.print("update_and_reboot from URL: ");
  Serial.println(url);
#endif

  url.replace("http://", "");
  url.replace(thinx_cloud_url, "");
  url.replace(":7442", ""); // warning, this should use existing vars!

#ifdef DEBUG
  if (logging)
    Serial.print("*TH: Update with URL: ");
  if (logging)
    Serial.println(url);
#endif

    // #define __USE_STREAM_UPDATER__ ; // Warning, this is MQTT-based streamed update!
#ifdef __USE_STREAM_UPDATER__
  if (logging)
    Serial.println(F("*TH: Starting MQTT & reboot..."));
  uint32_t size = pub.payload_len();

  if (_update_callback != nullptr)
  {
    _update_callback();
  }

  if (ESP.updateSketch(*pub.payload_stream(), size, true, false))
  {
    if (logging)
      Serial.println(F("Clearing retained message."));
    mqtt_client->publish(MQTT::Publish(pub.topic(), "").set_retain());
    mqtt_client->disconnect();

    if (logging)
      Serial.printf("Update Success: %lu\nRebooting...\n", millis() - startTime);

    notify_on_successful_update();
  }

  ESP.restart();
#else

  // TODO: Download the file and check expected_hash first...

  if (_update_callback != nullptr)
  {
    _update_callback();
  }

  if (logging)
    Serial.println(F("*TH: Starting ESP8266 HTTP Update & reboot..."));
  t_httpUpdate_return ret;

#ifndef __DISABLE_HTTPS__
  if (logging)
    Serial.println(F("*TH: HTTPS"));
  ret = ESPhttpUpdate.update(https_client, thinx_cloud_url, 7443, url.c_str());
#else
  if (logging)
    Serial.println(F("*TH: HTTP"));
  ret = ESPhttpUpdate.update(http_client, thinx_cloud_url, 7442, url, "");
#endif

  switch (ret)
  {
  case HTTP_UPDATE_FAILED:
    if (logging)
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
    setDashboardStatus(ESPhttpUpdate.getLastErrorString());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    if (logging)
      Serial.println(F("HTTP_UPDATE_NO_UPDATES"));
    break;

  case HTTP_UPDATE_OK:
    if (logging)
      Serial.println(F("HTTP_UPDATE_OK"));
    // Serial.println(F("Firmware update completed. Rebooting soon..."));
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

void THiNX::import_build_time_constants()
{

  // Only if not overridden by user
  if (strlen(thinx_api_key) < 4)
  {
    thinx_api_key = (char*)THINX_API_KEY;
  }

  if (strlen(THINX_UDID) > 2)
  {
    thinx_udid = (char*)THINX_UDID;
  }
  else
  {
    thinx_udid = (char*)"\0";
  }

// Use commit-id from thinx.h if not given by environment
#ifdef THX_COMMIT_ID
  thinx_commit_id = (char*)thx_commit_id;
#else
  thinx_commit_id = (char*)THINX_COMMIT_ID;
#endif

  thinx_alias = (char*)THINX_ALIAS;
  thinx_owner = (char*)THINX_OWNER;

  thinx_mqtt_port = THINX_MQTT_PORT;
  thinx_api_port = THINX_API_PORT;
  thinx_auto_update = THINX_AUTO_UPDATE;
  thinx_forced_update = THINX_FORCED_UPDATE;
  thinx_firmware_version = (char*)THINX_FIRMWARE_VERSION;
  thinx_firmware_version_short = (char*)THINX_FIRMWARE_VERSION_SHORT;
  app_version = (char*)THINX_APP_VERSION;
  env_hash = (char*)ENV_HASH;
}

/*
 * Performs the SPIFFS check and format if needed.
 */

bool THiNX::fsck()
{
#if defined(ESP8266)
  String realSize = String(ESP.getFlashChipRealSize());
  String ideSize = String(ESP.getFlashChipSize());
  bool flashCorrectlyConfigured = realSize.equals(ideSize);
#else
  bool flashCorrectlyConfigured = true;   //?
#endif
  bool fileSystemReady = false;
  if (flashCorrectlyConfigured == true)
  {
#if defined(ESP8266)
    fileSystemReady = SPIFFS.begin();
#else
    fileSystemReady = SPIFFS.begin(true); // formatOnFail=true
#endif
    if (!fileSystemReady)
    {
      if (logging)
        Serial.println(F("*TH: Formatting SPIFFS..."));
      fileSystemReady = SPIFFS.format();
      ;
      if (logging)
        Serial.println(F("*TH: Format complete, rebooting..."));
      Serial.flush();
      ESP.restart();
      return false;
    }
  }
  else
  {
    if (logging)
      Serial.print(F("*TH: Flash incorrectly configured, SPIFFS cannot start."));
#if defined(ESP8266)
    if (logging)
      Serial.println(" ide size: " + ideSize + ", real size: " + realSize);
#endif
  }
  return fileSystemReady ? true : false;
}

#ifdef __USE_WIFI_MANAGER__
/*
 * API key update event
 */

void THiNX::evt_save_api_key()
{
  if (should_save_config)
  {
    if (strlen(thx_api_key) > 4)
    {
      thinx_api_key = thx_api_key;
      // if (logging) Serial.print(F("Saving thx_api_key from Captive Portal."));
    }
    if (strlen(thx_owner_key) > 4)
    {
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

void THiNX::setPushConfigCallback(void (*func)(char *))
{
  _config_callback = func;
}

void THiNX::setFinalizeCallback(void (*func)(void))
{
  _finalize_callback = func;
}

void THiNX::setFirmwareUpdateCallback(void (*func)(void))
{
  _update_callback = func;
}

void THiNX::setMQTTCallback(void (*func)(byte *))
{
  _mqtt_callback = func;
}

void THiNX::setMQTTBroker(char *url, int port)
{
  thinx_mqtt_url = url;
  if (logging)
    Serial.println(F("Port is ignored, defaults to 1883"));
}

void THiNX::finalize()
{
  thinx_phase = COMPLETED;
  if (_finalize_callback)
  {
    _finalize_callback();
  }
  else
  {
    if (logging) Serial.println(F("*TH: Checkin completed (no _finalize_callback)."));
  }
}

/* This is necessary for SSL/TLS and should replace THiNX timestamp */
void THiNX::sync_sntp()
{
  // THiNX API returns timezone_offset in current DST, if applicable
  configTime(timezone_offset * 3600, 0, "0.europe.pool.ntp.org", "cz.pool.ntp.org");
  time_t now = time(nullptr);
  while (now < timezone_offset * 3600)
  {
    delay(500);
    if (logging) Serial.print(".");
    now = time(nullptr);
  }
  if (logging) Serial.println();

    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    if (logging) Serial.print(F("*TH: SNTP time: "));
    if (logging) Serial.print(asctime(&timeinfo));
}

void THiNX::setLocation(double lat, double lon)
{
  latitude = lat;
  longitude = lon;
  // uses mqtt_connected status because this happens only after first checkin
  // and thus prevents premature request to backend.
  if (wifi_connected && thinx_phase > FINALIZE)
  {
    // if (logging) Serial.println(F("*TH: LOOP » setLocation checkin"));
    checkin();
  }
}

void THiNX::setDashboardStatus(String newstatus)
{
  statusString = newstatus;
  if (wifi_connected && thinx_phase > FINALIZE)
  {
    // if (logging) Serial.println(F("*TH: LOOP » setDashboardStatus checkin"));
    checkin();
    if (mqtt_client)
    {
      String message = String("{ \"status\" : \"") + newstatus + String("\" }");
      mqtt_client->publish(mqtt_device_status_channel, message.c_str());
    }
  }
}

// deprecated since 2.2 (3)
void THiNX::setStatus(String newstatus)
{
  setDashboardStatus(newstatus);
}

void THiNX::setCheckinInterval(long interval)
{
  checkin_interval = interval;
}

void THiNX::setRebootInterval(long interval)
{
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

void THiNX::do_connect_wifi()
{
}

void THiNX::do_mqtt_connect()
{
}

void THiNX::do_mqtt_checkin()
{
}

void THiNX::do_connect_api()
{
}

void THiNX::do_deferred_update()
{
}

/*
 * Core loop
 */

void THiNX::loop()
{

  // printStackHeap("in");

  if (WiFi.status() != WL_CONNECTED)
  {
    wifi_connected = false;
    thinx_phase = CONNECT_WIFI;
  }

  if (thinx_phase == CONNECT_WIFI)
  {
    // If not connected manually or using WiFiManager, start connection in progress...
    if (WiFi.status() != WL_CONNECTED)
    {
      wifi_connected = false;
      if (wifi_connection_in_progress != true)
      {
        // if (logging) Serial.println(F("*TH: CONNECTING »"));
        connect(); // blocking
        wifi_connection_in_progress = true;
        return;
      }
      else
      {
        return;
      }
    }
    else
    {

      wifi_connected = true;
      wifi_connection_in_progress = false;

      // Synchronize SNTP time
      sync_sntp();

      // Start MDNS broadcast
#ifdef __DISABLE_PROXY__
#else
      if (!MDNS.begin(thinx_alias))
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: Error setting up mDNS"));
#endif
      }
      else
      {
        // Query MDNS proxy
        // if (logging) Serial.println(F("*TH: Searching for thinx-connect on local network..."));
        int n = MDNS.queryService("thinx", "tcp"); // TODO: WARNING! may be _tcp!
        if (n > 0)
        {
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
  if (thinx_phase == CHECKIN_MQTT)
  {
    init_thinx_mqtt_channel(); // initialize channel variable
    if (strlen(mqtt_device_channel) > 5)
    {
      if (mqtt_client->subscribe(mqtt_device_channel))
      {
#ifdef DEBUG
        // Serial.println(F("*TH: MQTT subscribed to device channel."));
#endif
        /*
        // Re-publish status on status topic?
        generate_checkin_body();
        mqtt_client->publish(
          mqtt_device_status_channel,
          json_buffer // F("{ \"status\" : \"connected\" }")
        );
        */
        mqtt_client->loop();
        delay(10);
        thinx_phase = FINALIZE;
        return;
      }
    }
  }

  if (thinx_phase == CONNECT_MQTT)
  {

    if (strlen(thinx_udid) > 4)
    {

      if (mqtt_connected == false)
      {
        mqtt_connected = start_mqtt();
        mqtt_client->loop();
        delay(10);
        if (mqtt_connected)
        {
          thinx_phase = CHECKIN_MQTT;
#ifdef DEBUG
          // Serial.println(F("*TH: MQTT connected immediately, changing phase to CHECKIN_MQTT..."));
#endif
          thinx_phase = CHECKIN_MQTT;
        }
        else
        {
          // tries again next time
        }
        return;
      }
      else
      {
#ifdef DEBUG
        Serial.println(F("*TH: MQTT already connected, changing phase to FINALIZE..."));
#endif
        thinx_phase = FINALIZE;
        return;
      }
    }
    else
    {
#ifdef DEBUG
      Serial.println(F("*TH: No UDID available for MQTT, skipping phase to FINALIZE..."));
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
  if (thinx_phase > FINALIZE)
  {
    if (millis() > checkin_time)
    {
      if (checkin_interval > 0)
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: LOOP » Checkin interval arrived..."));
#endif
        thinx_phase = CONNECT_API;
        checkin_interval = millis() + checkin_time;
      }
    }
  }

  // If connected, perform the MQTT loop and bail out ASAP
  if (thinx_phase == CONNECT_API)
  {
    if (WiFi.isConnected())
    {
      if (WiFi.getMode() != WIFI_STA)
      {
#ifdef DEBUG
        if (logging)
          Serial.println(F("*TH: LOOP « (AP_MODE)"));
#endif
        return;
      }
      if (strlen(thinx_api_key) > 4)
      {
        checkin(); // warning, this blocking and takes time, thus return...
        if (mqtt_connected == false)
        {
          thinx_phase = CONNECT_MQTT;
        }
        else
        {
#ifdef DEBUG
          if (logging)
            Serial.println(F("*TH: LOOP » FINALIZE (mqtt connected)"));
#endif
          thinx_phase = FINALIZE;
        }
      }
    }
  }

  if (thinx_phase == FINALIZE)
  {
    finalize();
  }

  if (thinx_phase >= FINALIZE)
  {
    if (mqtt_client)
    {
      mqtt_client->loop();
      delay(10);
    }
  }

  // deferred_update_url is set by response parser
  if (deferred_update_url.length() > 4)
  {
    if (ESP.getFreeHeap() > 2000)
    {
      update_and_reboot(deferred_update_url);
    }
    else
    {
      Serial.println(F("*TH: Not enough RAM, rebooting to gain more for update..."));
      ESP.restart();
    }
  }

  if ((reboot_interval > 0) && (millis() > reboot_interval))
  {
    setDashboardStatus(F("Rebooting..."));
    ESP.restart();
  }

#ifdef __USE_WIFI_MANAGER__
  // Save API key on change
  if (should_save_config)
  {
    // if (logging) Serial.println(F("*TH: Saving API key on change..."));
    evt_save_api_key();
    should_save_config = false;
  }
#endif

  // printStackHeap("out");
}

// ESP8266/ESP32 Specific Implementations

/*
 * Device MAC address
 */

#ifdef ESP32
const char *THiNX::thinx_mac()
{
  snprintf(mac_string, sizeof(mac_string), "5CCF7F%.6X", ESP.getEfuseMac());
  return mac_string;
}
#endif

#ifdef ESP8266
const char *THiNX::thinx_mac()
{
  snprintf(mac_string, sizeof(mac_string), "5CCF7F%.6X", ESP.getChipId());
  return mac_string;
}
#endif

bool THiNX::mem_check()
{
  if (ESP.getFreeHeap() < 3000)
  { // should be at least 4K for SSL!
#ifdef DEBUG
    Serial.println(F("*TH: Not enough RAM to checkin!"));
#endif
    return false;
  }
  return true;
}