#include "NodeRadio.h"

#include <Arduino.h>
#include <string.h>

namespace radiosensors {
namespace node {

NodeRadio::NodeRadio(const uint8_t chipSelect, const uint8_t interruptPin)
    : radio_(chipSelect, interruptPin, true) {}

bool NodeRadio::begin(const uint8_t nodeId, const uint8_t networkId) {
    const bool initialized =
        radio_.initialize(NODE_RFM69_FREQUENCY, nodeId, networkId);
    radio_.setHighPower(true);
    return initialized;
}

void NodeRadio::useCommissioningProfile(const uint8_t factoryKey[16]) {
    char key[17];
    memcpy(key, factoryKey, 16);
    key[16] = '\0';
    radio_.setAddress(0);
    radio_.setNetwork(0);
    radio_.encrypt(key);
    memset(key, 0, sizeof(key));
    radio_.setPowerLevel(NODE_DEFAULT_POWER_LEVEL);
}

void NodeRadio::useOperationalProfile(const storage::NetworkConfig& config) {
    char key[17];
    memcpy(key, config.installationKey, 16);
    key[16] = '\0';
    radio_.setAddress(config.nodeId);
    radio_.setNetwork(config.networkId);
    radio_.encrypt(key);
    radio_.setPowerLevel(config.powerLevel);
}

bool NodeRadio::sendTelemetry(
    const uint8_t gatewayId, const uint8_t* frame, const uint8_t size) {
    const bool acknowledged =
        radio_.sendWithRetry(gatewayId, frame, size, 2, 40);
    radio_.sleep();
    return acknowledged;
}

void NodeRadio::send(
    const uint8_t recipient, const uint8_t* frame, const uint8_t size) {
    radio_.send(recipient, frame, size, false);
}

bool NodeRadio::receive(
    const uint32_t timeoutMs, const uint8_t expectedSender, uint8_t* output,
    const uint8_t expectedSize) {
    const uint32_t started = millis();
    radio_.receiveDone();
    while (static_cast<uint32_t>(millis() - started) < timeoutMs) {
        if (!radio_.receiveDone()) continue;
        if (radio_.SENDERID == expectedSender &&
            radio_.DATALEN == expectedSize) {
            memcpy(output, radio_.DATA, expectedSize);
            return true;
        }
        radio_.receiveDone();
    }
    return false;
}

void NodeRadio::sleep() {
    radio_.sleep();
}

}  // namespace node
}  // namespace radiosensors
