#include <Arduino.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

//#define DEBUG                     // takes 8k of sketch and 1+1k of stack/heap size (when measured last time)
#define __DISABLE_HTTPS__         // to save memory if needed
//#define __ENABLE_WIFI_MIGRATION__ // enable automatic WiFi disconnect/reconnect on Configuration Push (THINX_ENV_SSID and THINX_ENV_PASS)
//#define __USE_WIFI_MANAGER__ // if disabled, you need to `WiFi.begin(ssid, pass)` on your own; saves about 3% of sketch space, excludes DNSServer and WebServer
#define __USE_SPIFFS__    // if disabled, uses EEPROM instead
#define __DISABLE_PROXY__ // skips using Proxy until required (security measure)

#ifndef THX_REVISION
#ifdef THINX_FIRMWARE_VERSION_SHORT
#define THX_REVISION THINX_FIRMWARE_VERSION_SHORT
#else
#define THX_REVISION "450"
#endif
#endif

// Provides placeholder for THINX_FIRMWARE_VERSION_SHORT
#ifndef VERSION
#define VERSION "2.9." THX_REVISION
#endif

#ifdef __USE_WIFI_MANAGER__
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#endif

#include <stdio.h>
#include <time.h>

#include <FS.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

// Using better than Arduino-bundled version of MQTT https://github.com/Imroy/pubsubclient
#include <PubSubClient.h>

//#include "sha256.h"
#include "ESPCompatibility.h"

class THiNX
{
public:
    static bool logging;   // disable all logging to prevent interference with serial comm
    static double latitude;
    static double longitude;
    static String statusString;
    static String accessPointName;
    static String accessPointPassword;

    static char *thinx_mqtt_url;
    static char *thinx_cloud_url; // up to 1k but generally something where FQDN fits

    static String lastWill;

    static double benchmark_speed;

#ifdef __USE_WIFI_MANAGER__
    static WiFiManagerParameter *api_key_param;
    static WiFiManagerParameter *owner_param;
    static int should_save_config; // after autoconnect, may provide new API Key
    static void saveConfigCallback();
#endif

    THiNX();
    THiNX(const char *__apikey);
    THiNX(const char *__apikey, const char *__owner_id);

    enum payload_type
    {
        Unknown = 0,
        UPDATE = 1,        // Firmware Update Response Payload
        REGISTRATION = 2,  // Registration Response Payload
        NOTIFICATION = 3,  // Notification/Interaction Response Payload
        CONFIGURATION = 4, // Environment variables update
        Reserved = 255,    // Reserved
    };

    enum phase
    {
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
    void generate_checkin_body(); // uses static json_buffer

    // MQTT
    PubSubClient *mqtt_client = nullptr;
    char mqtt_device_channel[128];
    char mqtt_device_channels[128];
    char mqtt_device_status_channel[128];
    void init_thinx_mqtt_channel();
    String thinx_mqtt_channels();
    String thinx_mqtt_channel();
    char *generate_mqtt_status_channel(); // generate

    // Values imported on from thinx.h
    const char *app_version;                  // max 80 bytes
    const char *available_update_url;         // up to 1k
    const char *thinx_commit_id;              // 40 bytes + 1
    const char *thinx_firmware_version_short; // 14 bytes
    const char *thinx_firmware_version;       // max 80 bytes
    const char *env_hash;                     // max 65 bytes; SHA256 of environment.h JSON source

    const char *thinx_version_id; // max 80 bytes (DEPRECATED?)

    bool thinx_auto_update;
    bool thinx_forced_update;

    long thinx_mqtt_port;
    long thinx_api_port;

    // dynamic variables
    char *thinx_alias;
    char *thinx_owner;

    char *get_udid();

    void setPushConfigCallback(void (*func)(char *));
    void setFinalizeCallback(void (*func)(void));
    void setFirmwareUpdateCallback(void (*func)(void));
    void setMQTTCallback(void (*func)(byte *));
    void setMQTTBroker(char *url, int port);
    void setLastWill(const String &nextWill); // disconnect MQTT and reconnect with different lastWill than default

    bool wifi_connection_in_progress;
    unsigned long wifi_conection_timeout = 0;

    // MQTT Support

    // publish to device status topic only

    void publish_status(const char *message, bool retain); // send string to status topic, set retain
    void publish_status_unretained(const char *message);   // send string to status topic, unretained

    // publish to specified topic
    void publish(const String &, const String &, bool); // send String to any channel, optinally with retain
    void publish(char *message, char *topic, bool retain);

    static const char time_format[];
    static const char date_format[];

    unsigned long epoch();           // estimated timestamp since last checkin as
    void setCheckinInterval(long interval);
    void setRebootInterval(long interval);

    // checkins
    void checkin();                   // happens on registration
    void setDashboardStatus(String);  // performs checkin while updating Status on Dashboard
    void setStatus(String);           // deprecated 2.2 (3)
    void setLocation(double, double); // performs checkin while updating Location

    bool wifi_connected; // WiFi connected in station mode
    bool mqtt_connected; // success or failure on subscription
    unsigned long mqtt_reconnect_timeout;

private:
    // Memory allocation debugging
    static uint32_t last_free_heap_size;
    void printStackHeap(const String &tag);

    unsigned long bytes_sent = 0;

    bool info_loaded = false;
    static char *thinx_api_key;
    static char *thinx_owner_key;
    char *thinx_udid;

    //
    // Build-specific constants (override for Arduino IDE which does not set any Environments like PlatformIO)
    //

#ifdef THX_REVISION
    const char *thx_revision = THX_REVISION;
#else
    const char *thx_revision = "0";
#endif

#ifdef THX_COMMIT_ID
    const char *thx_commit_id = THX_COMMIT_ID;
#else
    const char *thx_commit_id = "unknown";
#endif

    //
    // THiNXLib
    //

    void configCallback();

    // WiFi Manager
    WiFiClient http_client;

#ifndef __DISABLE_HTTPS__
    BearSSL::WiFiClientSecure https_client;
#endif

    int status; // global WiFi status
    bool once;  // once token for initialization

    // THiNX API
    static char thx_api_key[65];   // static due to accesibility to WiFiManager
    static char thx_owner_key[65]; // static due to accesibility to WiFiManager

    const char *thinx_mac();

    static char json_buffer[768]; // statically allocated to prevent fragmentation, should be rather dynamic

    // In order of appearance
    bool fsck();         // check filesystem if using SPIFFS
    void connect();      // start the connect loop
    void connect_wifi(); // start connecting

#ifdef __DISABLE_HTTPS__
    void fetch_data(WiFiClient *client); // fetch and parse; max return char[] later
    void send_data(const String &);        // HTTP
#else
    void send_data_secure(const String &); // HTTPS
    void fetch_data_secure(BearSSL::WiFiClientSecure *client);
#endif

    void parse(const char *); // needs to be refactored to char[] from String
    void update_and_reboot(String);

    int timezone_offset = 0;                       // should use simpleDSTadjust
    unsigned long checkin_interval = 86400 * 1000; // next timeout millis()
    unsigned long checkin_time = 86400 * 1000;     // can be set externaly, defaults to 1h (3600 * 1000)

    unsigned long last_checkin_millis;
    unsigned long last_checkin_timestamp;

    unsigned long reboot_timeout = 86400 * 1000;  // next timeout millis()
    unsigned long reboot_interval = 86400 * 1000; // can be set externaly, defaults to 24h

    // MQTT
    bool start_mqtt(); // connect to broker and subscribe

    int performed_mqtt_checkin; // one-time flag
    int all_done;               // finalize flag

    void (*_config_callback)(char *) = NULL; // Called when server pushes new environment vars using MQTT
    void (*_mqtt_callback)(byte *) = NULL;
    void (*_update_callback)(void) = NULL;

    // Data Storage
    void import_build_time_constants(); // sets variables from thinx.h file
    void save_device_info();            // saves variables to SPIFFS or EEPROM
    void restore_device_info();         // reads variables from SPIFFS or EEPROM

    // Updates
    void notify_on_successful_update(); // send a MQTT notification back to Web UI

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