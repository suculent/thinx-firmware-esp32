#include "ESPCompatibility.h"

//#define ESP32 // debug only

char * ESPCompatibility::mac = "FFFFFFFFFFFF";
char * ESPCompatibility::fcid = "FFFFFFFFFFFF";

char * ESPCompatibility::get_mac_id()
{
  String macAddress = WiFi.macAddress();
  macAddress.replace(":", "");
  macAddress.replace(" ", "");
  sprintf(ESPCompatibility::mac, "%s", macAddress.c_str());

  return ESPCompatibility::mac;
}

char * ESPCompatibility::get_flash_id()
{
#ifdef ESP8266
  snprintf(ESPCompatibility::fcid, 12, "%.6X", ESP.getFlashChipId());
#endif
#ifdef ESP32
  uint64_t macAddress = ESP.getEfuseMac();
  uint64_t macAddressTrunc = macAddress << 40;
  uint32_t chipID = macAddressTrunc >> 40;
  snprintf(ESPCompatibility::fcid, 13, "5CCF7F%.6lX", chipID);
#endif

  return ESPCompatibility::fcid;
}

char *ESPCompatibility::mac_id()
{
  ESPCompatibility::mac = get_mac_id();
  return ESPCompatibility::mac;
}

char * ESPCompatibility::flash_id()
{
  ESPCompatibility::fcid = get_flash_id();
  return ESPCompatibility::fcid;
}