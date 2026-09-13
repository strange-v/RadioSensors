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
    // The RFM69 releases MISO while deselected, which would leave the input
    // floating through every sleep. The weak pull-up does not disturb SPI.
    pinMode(PIN_SPI_MISO, INPUT_PULLUP);
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
    radio_.setPowerLevel(NODE_RADIO_MAX_POWER_LEVEL);
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

void NodeRadio::setPowerLevel(const uint8_t level) {
    radio_.setPowerLevel(level);
}

bool NodeRadio::sendTelemetry(
    const uint8_t gatewayId, const uint8_t* frame, const uint8_t size,
    protocol::TelemetryAck& ack, int16_t& ackRssi) {
    const bool acknowledged =
        radio_.sendWithRetry(gatewayId, frame, size, 2, 40);
    // The acknowledgement stays in DATA and RSSI until the radio receives
    // again.
    ack = acknowledged
        ? protocol::decodeTelemetryAck(radio_.DATA, radio_.DATALEN)
        : protocol::TelemetryAck{false, false, 0};
    if (acknowledged) ackRssi = radio_.RSSI;
    radio_.sleep();
    return acknowledged;
}

bool NodeRadio::sendAcknowledged(
    const uint8_t recipient, const uint8_t* frame, const uint8_t size) {
    return radio_.sendWithRetry(recipient, frame, size, 2, 40);
}

void NodeRadio::send(
    const uint8_t recipient, const uint8_t* frame, const uint8_t size) {
    radio_.send(recipient, frame, size, false);
}

bool NodeRadio::receive(
    const uint32_t timeoutMs, const uint8_t expectedSender, uint8_t* output,
    const uint8_t expectedSize) {
    return receiveMatching(
               timeoutMs, expectedSender, output, expectedSize,
               expectedSize) == expectedSize;
}

uint8_t NodeRadio::receiveFrame(
    const uint32_t timeoutMs, const uint8_t expectedSender, uint8_t* output,
    const uint8_t capacity) {
    return receiveMatching(timeoutMs, expectedSender, output, 1, capacity);
}

uint8_t NodeRadio::receiveMatching(
    const uint32_t timeoutMs, const uint8_t expectedSender, uint8_t* output,
    const uint8_t minimumSize, const uint8_t maximumSize) {
    const uint32_t started = millis();
    radio_.receiveDone();
    while (static_cast<uint32_t>(millis() - started) < timeoutMs) {
        if (!radio_.receiveDone()) continue;
        const uint8_t size = radio_.DATALEN;
        if (radio_.SENDERID == expectedSender && size >= minimumSize &&
            size <= maximumSize) {
            memcpy(output, radio_.DATA, size);
            return size;
        }
        radio_.receiveDone();
    }
    return 0;
}

void NodeRadio::sleep() {
    radio_.sleep();
}

}  // namespace node
}  // namespace radiosensors
