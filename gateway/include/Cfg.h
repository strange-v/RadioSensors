#ifndef CFG_h
#define CFG_h
#include <Arduino.h>

namespace Cfg
{
    const char haDiscoveryPrefix[] = "homeassistant";
    const char gatewayTopic[] = "rfm_gateway/%s/state";
    const char nodeTopic[] = "rfm_gateway/%s/node/%d";

    const char name[] = "Radio Gateway";
    const char manufacturer[] = "Just Testing";
    const char model[] = "RG1";
    const char version[] = "1.0.0";
}
#endif