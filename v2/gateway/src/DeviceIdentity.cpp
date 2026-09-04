#include "DeviceIdentity.h"

#include <Arduino.h>
#include <esp_mac.h>

namespace gateway::identity {
namespace {

char gatewayHostname[32] = "rf-gateway-uninitialized";

}  // namespace

void begin() {
    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        snprintf(gatewayHostname, sizeof(gatewayHostname), "rf-gateway-unknown");
        return;
    }

    snprintf(
        gatewayHostname,
        sizeof(gatewayHostname),
        "rf-gateway-%02x%02x%02x%02x%02x%02x",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);
}

const char* hostname() {
    return gatewayHostname;
}

}  // namespace gateway::identity
