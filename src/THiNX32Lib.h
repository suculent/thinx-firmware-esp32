#include <Arduino.h>

#define DEBUG // takes 8k of sketch and 1+1k of stack/heap size (when measured last time)
#define __ENABLE_WIFI_MIGRATION__ // enable automatic WiFi disconnect/reconnect on Configuration Push (THINX_ENV_SSID and THINX_ENV_PASS)
// #define __USE_WIFI_MANAGER__ // if disabled, you need to `WiFi.begin(ssid, pass)` on your own; saves about 3% of sketch space, excludes DNSServer and WebServer
#define __USE_SPIFFS__ // if disabled, uses EEPROM instead
#define __DISABLE_PROXY__ // skips using Proxy until required (security measure)

// Provides placeholder for THINX_FIRMWARE_VERSION_SHORT
#ifndef VERSION
#define VERSION "2.8.243"
#endif

#ifndef THX_REVISION
#ifdef THINX_FIRMWARE_VERSION_SHORT
#define THX_REVISION THINX_FIRMWARE_VERSION_SHORT
#else
#define THX_REVISION "243"
#endif
#endif

#ifdef __USE_WIFI_MANAGER__
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>
#endif

// extern C
#include <stdio.h>
#include <time.h>

// Supported by Arduino ESP32 Framwork
#include <SPIFFS.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ESP32httpUpdate.h> // already part of firmware, deprecated
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

// Using better than Arduino-bundled version of MQTT https://github.com/Imroy/pubsubclient
#include <PubSubClient.h>

//#include "sha256.h"

class THiNX {

public:

    static bool logging;                          // disable all logging to prevent interference with serial comm
    static bool forceHTTP;                        // set to true for disabling HTTPS
    static double latitude;
    static double longitude;
    static String statusString;
    static String accessPointName;
    static String accessPointPassword;

    static char* thinx_mqtt_url;
    static char* thinx_cloud_url;

    static String lastWill;

#ifdef __USE_WIFI_MANAGER__
    static WiFiManagerParameter *api_key_param;
    static WiFiManagerParameter *owner_param;
    static int should_save_config; // after autoconnect, may provide new API Key
    static void saveConfigCallback();
#endif

    THiNX();
    THiNX(const char * __apikey, const char * __owner_id);
    THiNX(const char * __apikey);

    enum payload_type {
        Unknown = 0,
        UPDATE = 1,                                 // Firmware Update Response Payload
        REGISTRATION = 2,                           // Registration Response Payload
        NOTIFICATION = 3,                      // Notification/Interaction Response Payload
        CONFIGURATION = 4,                     // Environment variables update
        Reserved = 255,                             // Reserved
    };

    enum phase {
        INIT = 0,
        CONNECT_WIFI = 1,
        CONNECT_API = 2,
        CONNECT_MQTT = 3,
        CHECKIN_MQTT = 4,
        FINALIZE = 5,
        COMPLETED = 6
    };

    phase thinx_phase;

    // Public API
    void init_with_api_key(const char *);
    void loop();

    char mac_string[17];
    void generate_checkin_body();

    // MQTT
    PubSubClient * mqtt_client = nullptr;
    char mqtt_device_channel[128];
    char mqtt_device_channels[128];
    char mqtt_device_status_channel[128];
    String thinx_mqtt_channel();
    String thinx_mqtt_channels();
    String thinx_mqtt_status_channel();

    // Values imported on from thinx.h
    const char* app_version;                  // max 80 bytes
    const char* available_update_url;         // up to 1k
    const char* thinx_commit_id;              // 40 bytes + 1
    const char* thinx_firmware_version_short; // 14 bytes
    const char* thinx_firmware_version;       // max 80 bytes

    const char* thinx_version_id;             // max 80 bytes (DEPRECATED?)

    bool thinx_auto_update;
    bool thinx_forced_update;

    long thinx_mqtt_port;
    long thinx_api_port;

    // dynamic variables
    char* thinx_alias;
    char* thinx_owner;

    char * get_udid();

    void setPushConfigCallback( void (*func)(String) );
    void setFinalizeCallback( void (*func)(void) );
    void setFirmwareUpdateCallback( void (*func)(void) );
    void setMQTTCallback( void (*func)(byte*) );
    void setMQTTBroker( char * url, int port );
    void setLastWill(String nextWill);        // disconnect MQTT and reconnect with different lastWill than default

    bool wifi_connection_in_progress = false;

    // MQTT Support

    // publish to device status topic only
    void publish_status(const char *message, bool retain=false);  // send string to status topic, set retain

    // publish to specified topic
    void publish(char * message, char * topic, bool retain);

    static const char time_format[];
    static const char date_format[];

    unsigned long epoch();                    // estimated timestamp since last checkin as
    String thinx_time(const char*);                            // estimated current Time
    String thinx_date(const char*);                            // estimated current Date
    void setCheckinInterval(long interval);
    void setRebootInterval(long interval);

    // checkins
    void checkin();                         // happens on registration
    void setDashboardStatus(String);        // performs checkin while updating Status on Dashboard
    void setStatus(String);                 // deprecated 2.2 (3)
    void setLocation(double,double);        // performs checkin while updating Location

    bool wifi_connected;                         // WiFi connected in station mode
    int  mqtt_connected;                    // success or failure on subscription

private:

    // Memory allocation debugging
    static uint32_t last_free_heap_size;
    void printStackHeap(String tag);

    bool info_loaded = false;
    static char* thinx_api_key;
    static char* thinx_owner_key;
    char* thinx_udid;

    //
    // Build-specific constants (override for Arduino IDE which does not set any Environments like PlatformIO)
    //

#ifdef THX_REVISION
    const char* thx_revision = strdup(THX_REVISION);
#else
    const char* thx_revision = "0";
#endif

#ifdef THX_COMMIT_ID
    const char* thx_commit_id = strdup(THX_COMMIT_ID);
#else
    const char* thx_commit_id = "unknown";
#endif

    //
    // THiNXLib
    //

    void configCallback();

    // WiFi Manager
    WiFiClient http_client;
    WiFiClientSecure https_client;
    int status;                             // global WiFi status
    bool once;                              // once token for initialization

    // THiNX API
    static char thx_api_key[65];            // static due to accesibility to WiFiManager
    static char thx_owner_key[65];          // static due to accesibility to WiFiManager

    const char * thinx_mac();

    static char json_buffer[512];       // statically allocated to prevent fragmentation, should be rather dynamic

    // In order of appearance
    bool fsck();                              // check filesystem if using SPIFFS
    void connect();                           // start the connect loop
    void connect_wifi();                      // start connecting

    void senddata(String);                    // HTTP, will deprecate?
    void send_data(String);                   // HTTPS
    void fetchdata();                        // fetch and parse; max return char[] later
    void fetch_data();                        // fetch and parse; max return char[] later
    void parse(const char*);                     // needs to be refactored to char[] from String
    void update_and_reboot(String);

    int timezone_offset = 0; // should use simpleDSTadjust
    unsigned long checkin_interval = 86400 * 1000;          // next timeout millis()
    unsigned long checkin_time = 86400 * 1000;  // can be set externaly, defaults to 1h (3600 * 1000)

    unsigned long last_checkin_millis;
    unsigned long last_checkin_timestamp;

    unsigned long reboot_timeout = 86400 * 1000;          // next timeout millis()
    unsigned long reboot_interval = 86400 * 1000;  // can be set externaly, defaults to 24h

    // MQTT
    bool start_mqtt();                      // connect to broker and subscribe

    int performed_mqtt_checkin;              // one-time flag
    int all_done;                              // finalize flag

    void (*_config_callback)(String) = NULL;  // Called when server pushes new environment vars using MQTT
    void (*_mqtt_callback)(byte*) = NULL;
    void (*_update_callback)(void) = NULL;

    // Data Storage
    void import_build_time_constants();     // sets variables from thinx.h file
    void save_device_info();                // saves variables to SPIFFS or EEPROM
    void restore_device_info();             // reads variables from SPIFFS or EEPROM

    // Updates
    void notify_on_successful_update();     // send a MQTT notification back to Web UI

    // Event Queue / States
    int mqtt_started;
    bool complete;
    void evt_save_api_key();

    // Finalize
    void (*_finalize_callback)(void) = NULL;
    void finalize(); // Complete the checkin, schedule, callback...

    // Local WiFi Impl
    bool wifi_wait_for_connect;
    unsigned long wifi_wait_start;
    unsigned long wifi_wait_timeout;
    int wifi_retry;
    uint8_t wifi_status;

    // SHA256
    //bool check_hash(char * filename, char * expected);
    //char * expected_hash;
    //char * expected_md5;

    // Required for SSL/TLS: sync time using SNTP first. TODO: Remove duplicate impl.
    void sync_sntp();

    String deferred_update_url;

    void do_connect_wifi();
    void do_mqtt_connect();
    void do_mqtt_checkin();
    void do_connect_api();
    void do_deferred_update();

    bool mem_check();

};
