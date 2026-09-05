#include <Arduino.h>

#include "BoardProfile.h"
#include "CommissioningService.h"
#include "Diagnostics.h"
#include "DeviceIdentity.h"
#include "EthernetService.h"
#include "HealthServer.h"
#include "GatewayStatus.h"
#include "NodeRegistryStore.h"
#include "OtaService.h"
#include "RadioService.h"

namespace {

constexpr uint32_t kSerialStartupDelayMs = 1000;

const char* ethernetControllerName(gateway::board::EthernetController controller) {
    switch (controller) {
        case gateway::board::EthernetController::Lan8720:
            return "LAN8720 (RMII)";
        case gateway::board::EthernetController::W5500:
            return "W5500 (SPI)";
    }

    return "unknown";
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(kSerialStartupDelayMs);

    Serial.println();
    gateway::identity::begin();
    Serial.println("RadioSensors gateway");
    Serial.printf("Firmware: %s\n", GATEWAY_FIRMWARE_VERSION);
    Serial.printf("Board: %s\n", gateway::board::current.name);
    Serial.printf(
        "Ethernet: %s\n",
        ethernetControllerName(gateway::board::current.ethernetController));
    Serial.printf("PoE profile: %s\n", gateway::board::current.hasPoe ? "yes" : "no");
    Serial.printf("Reset reason: %s\n", gateway::diagnostics::resetReason());
    Serial.printf("Hostname: %s\n", gateway::identity::hostname());

    const bool watchdogStarted = gateway::diagnostics::beginWatchdog();
    Serial.printf("Task watchdog: %s\n", watchdogStarted ? "enabled" : "failed");

    gateway::registry_store::begin();
    gateway::status::begin();
    gateway::ethernet::begin();
    gateway::radio::begin();
    gateway::commissioning::begin();
    gateway::health::begin();
    gateway::ota::begin();
}

void loop() {
    gateway::status::loop();
    gateway::ota::loop();
    gateway::diagnostics::feedWatchdog();
    delay(100);
}
