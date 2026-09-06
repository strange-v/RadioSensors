#include "MdnsService.h"

#include <ESPmDNS.h>

#include "ApiVersion.h"
#include "BoardProfile.h"
#include "ConfigurationStore.h"
#include "DeviceIdentity.h"
#include "EthernetService.h"
#include "FirmwareVersion.h"

namespace gateway::mdns_service {
namespace {

bool active = false;
uint32_t appliedGeneration = UINT32_MAX;
uint32_t retryAt = 0;

void stop() {
    if (!active) return;
    MDNS.end();
    active = false;
    Serial.println("mDNS stopped");
}

void apply() {
    const auto settings = configuration_store::settings();
    const uint32_t generation = configuration_store::settingsGeneration();
    if (!settings.mdnsEnabled || !ethernet::hasIp()) {
        stop();
        appliedGeneration = generation;
        return;
    }
    if (active && appliedGeneration == generation) return;
    if (!active && appliedGeneration == generation &&
        static_cast<int32_t>(millis() - retryAt) < 0) return;
    stop();
    if (!MDNS.begin(identity::hostname())) {
        Serial.println("mDNS start failed");
        appliedGeneration = generation;
        retryAt = millis() + 5000;
        return;
    }
    MDNS.addService("radiosensors", "tcp", 80);
    MDNS.addServiceTxt("radiosensors", "tcp", "api", String(api::version));
    MDNS.addServiceTxt(
        "radiosensors", "tcp", "api_version", String(api::version));
    MDNS.addServiceTxt(
        "radiosensors", "tcp", "gateway_id", identity::gatewayId());
    MDNS.addServiceTxt(
        "radiosensors", "tcp", "boot_id", identity::bootId());
    MDNS.addServiceTxt("radiosensors", "tcp", "firmware", firmware::version);
    MDNS.addServiceTxt("radiosensors", "tcp", "board", board::current.name);
    MDNS.addServiceTxt(
        "radiosensors", "tcp", "hostname", identity::hostname());
    active = true;
    appliedGeneration = generation;
    retryAt = 0;
    Serial.printf("mDNS advertising %s.local\n", identity::hostname());
}

}  // namespace

void begin() { apply(); }
void loop() { apply(); }

}  // namespace gateway::mdns_service
