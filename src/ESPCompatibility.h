#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
class ESPCompatibility {

    static char * get_mac_id();
    static char * get_flash_id();

  public:

    static char * mac;  // [13]
    static char * fcid; // [13]

    static char* mac_id();
    static char* flash_id();
};
