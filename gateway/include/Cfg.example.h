#ifndef CFG_h
#define CFG_h
#include <stdint.h>
#include <IPAddress.h>

namespace Cfg
{
    const char wifiSsid[] = "SSID";
    const char wifiPassword[] = "password";

    const char mqttNodeId[] = "GW_NODE_ID1";
    const IPAddress mqttHost(192, 168, 0, 1);
    const uint16_t mqttPort = 1883;
    const char mqttUser[] = "admin";
    const char mqttPassword[] = "admin";

    const char otaPassword[] = "OTA_Password";

    const char radioEncriptionKey[] = "sampleEncryptKey";

    const uint8_t idMaxGroupA = 10;
    const uint8_t idMaxGroupB = 20;
}
#endif
