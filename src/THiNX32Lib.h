#include <Arduino.h>

#define __DEBUG__ // enables stack/heap debugging

#define __ENABLE_WIFI_MIGRATION__ // enable automatic WiFi disconnect/reconnect on Configuration Push (THINX_ENV_SSID and THINX_ENV_PASS)
#define __USE_WIFI_MANAGER__ // if disabled, you need to `WiFi.begin(ssid, pass)` on your own
#define __USE_SPIFFS__ // if disabled, uses EEPROM instead
#define __DISABLE_PROXY__ // skips using Proxy until required (security measure)
#
// Provides placeholder for THINX_FIRMWARE_VERSION_SHORT
#ifndef VERSION
#define VERSION "2.4.189"
#endif

#ifndef THX_REVISION
#ifdef THINX_FIRMWARE_VERSION_SHORT
#define THX_REVISION THINX_FIRMWARE_VERSION_SHORT
#else
#define THX_REVISION "189"
#endif
#endif

#ifdef __USE_WIFI_MANAGER__
#include <DNSServer.h>
#ifdef ESP8266
#include <ESP8266WebServer.h>
#else
#include <WebServer.h>
#endif
#include <WiFiManager.h>
#endif

#include <stdio.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval

#include <EEPROM.h>

#ifdef ESP8266
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#else
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ESP32httpUpdate.h>
#endif

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Using better than Arduino-bundled version of MQTT https://github.com/Imroy/pubsubclient
#include <PubSubClient.h>

#include "sha256.h"

class THiNX {

    public:

    // Root certificate used by backend
    static const char * thx_ca_cert;

    static bool forceHTTP;
    static double latitude;
    static double longitude;
    static String statusString;
    static String accessPointName;
    static String accessPointPassword;
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
        UPDATE = 1,         // Firmware Update Response Payload
        REGISTRATION = 2,   // Registration Response Payload
        NOTIFICATION = 3,   // Notification/Interaction Response Payload
        CONFIGURATION = 4,  // Environment variables update
        Reserved = 255,     // Reserved
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

    String checkin_body();  // TODO: Refactor to C-string

    // MQTT
    PubSubClient *mqtt_client = nullptr;

    char mqtt_device_channel[128];
    char mqtt_device_channels[128];
    char mqtt_device_status_channel[128];
    String thinx_mqtt_channel();
    String thinx_mqtt_channels();
    String thinx_mqtt_status_channel();

    // Values imported on build from thinx.h
    const char* app_version;                  // max 80 bytes
    const char* available_update_url;         // up to 1k
    const char* thinx_cloud_url;              // up to 1k but generally something where FQDN fits
    const char* thinx_commit_id;              // 40 bytes + 1
    const char* thinx_firmware_version_short; // 14 bytes
    const char* thinx_firmware_version;       // max 80 bytes
    const char* thinx_mqtt_url;               // up to 1k but generally something where FQDN fits
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
    void setMQTTCallback( void (*func)(String) );
    void setLastWill(String nextWill);        // disconnect and reconnect updating the lastWill

    int wifi_connection_in_progress;

    // MQTT Support

    // publish to device status topic only
    void publishStatus(String);              // DEPRECATED, send String to status topic
    void publishStatusUnretained(String);    // DEPRECATED, send String to status topic (unretained)
    void publishStatusRetain(String, bool);  // DEPRECATED, send String to status topic (optionally retained)
    void publish_status(char *message, bool retain);  // send string to status topic, set retain
    void publish_status_unretained(char *);  // send string to status topic, unretained

    // publish to specified channel
    void publish(String, String, bool);      // DEPRECARED, send String to any channel, optinally with retain
    void publish(char * message, char * topic, bool retain);

    static const char time_format[];
    static const char date_format[];

    // Time and Date support requires checkin against THiNX API > 0.9.305x
    String thinx_time(const char*);         // estimated current Time
    String thinx_date(const char*);         // estimated current Date

    void setCheckinInterval(long interval);
    void setRebootInterval(long interval);

    // checkins
    void checkin();                         // happens on registrationw
    void setDashboardStatus(String);        // performs checkin while updating Status on Dashboard
    void setStatus(String);                 // deprecated 2.2 (3)
    void setLocation(double,double);        // performs checkin while updating Location

    private:

    char* thinx_udid;

    bool wifi_connected;  // WiFi connected in station mode
    bool info_loaded = false;

    static char* thinx_api_key;
    static char* thinx_owner_key;

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
    WiFiClient thx_wifi_client;
    WiFiClientSecure https_client;
    int status;                             // global WiFi status
    bool once;                              // once token for initialization

    // THiNX API
    static char thx_api_key[65];            // static due to accesibility to WiFiManager
    static char thx_owner_key[65];          // static due to accesibility to WiFiManager

    char mac_string[17];
    const char * thinx_mac();

    char json_info[512] = {0};               // statically allocated to prevent fragmentation

    String json_output;

    // In order of appearance

    bool fsck();                            // check filesystem if using SPIFFS

    void connect();                         // start the connect loop
    void connect_wifi();                    // start connecting

    void senddata(String);                  // HTTP, will deprecate?
    void send_data(String);                 // HTTPS
    void fetch_data();                      // fetch and parse; max return char[] later
    void parse(String);                     // needs to be refactored to char[] from String
    void update_and_reboot(String);

    int timezone_offset = 2;
    unsigned long checkin_timeout = 3600 * 1000;                   // next timeout millis()
    unsigned long checkin_interval = 3600 * 1000;    // can be set externaly, defaults to 1h

    unsigned long last_checkin_millis;
    unsigned long last_checkin_timestamp;

    unsigned long reboot_timeout = 86400 * 1000;                    // next timeout millis()
    unsigned long reboot_interval = 86400 * 1000;    // can be set externaly, defaults to 24h

    // MQTT
    bool start_mqtt();                      // connect to broker and subscribe
    int mqtt_connected;                     // success or failure on subscription
    String mqtt_payload;                    // mqtt_payload store for parsing
    int performed_mqtt_checkin;             // one-time flag
    int all_done;                           // finalize flag

    void (*_config_callback)(String) = NULL;  // Called when server pushes new environment vars using MQTT
    void (*_mqtt_callback)(String) = NULL;

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
    void finalize();                        // Complete the checkin, schedule, callback...

    // Local WiFi Impl
    bool wifi_wait_for_connect;
    unsigned long wifi_wait_start;
    unsigned long wifi_wait_timeout;
    int wifi_retry;
    uint8_t wifi_status;

    // SHA256
    bool check_hash(char * filename, char * expected);
    char * expected_hash;
    char * expected_md5;

    // SSL/TLS
    void sync_sntp();                     // Synchronize time using SNTP instead of THiNX

    // debug
    void printStackHeap(String);
};
