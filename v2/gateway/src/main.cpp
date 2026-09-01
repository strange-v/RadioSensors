#include <Arduino.h>

#include "BoardProfile.h"

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
    Serial.println("RadioSensors gateway");
    Serial.printf("Firmware: %s\n", GATEWAY_FIRMWARE_VERSION);
    Serial.printf("Board: %s\n", gateway::board::current.name);
    Serial.printf(
        "Ethernet: %s\n",
        ethernetControllerName(gateway::board::current.ethernetController));
    Serial.printf("PoE profile: %s\n", gateway::board::current.hasPoe ? "yes" : "no");
}

void loop() {
    delay(1000);
}

