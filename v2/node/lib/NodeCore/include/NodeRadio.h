#pragma once

#include <RFM69.h>
#include <TelemetryFrames.h>
#include <stddef.h>
#include <stdint.h>

#include "NodeStorage.h"

namespace radiosensors {
namespace node {

class NodeRadio {
public:
    NodeRadio(uint8_t chipSelect, uint8_t interruptPin);

    bool begin(uint8_t nodeId, uint8_t networkId);
    void useCommissioningProfile(const uint8_t factoryKey[16]);
    void useOperationalProfile(const storage::NetworkConfig& config);
    void setPowerLevel(uint8_t level);
    // `ack` is what the acknowledgement carried, `ackRssi` its strength.
    bool sendTelemetry(
        uint8_t gatewayId, const uint8_t* frame, uint8_t size,
        protocol::TelemetryAck& ack, int16_t& ackRssi);
    bool sendAcknowledged(uint8_t recipient, const uint8_t* frame, uint8_t size);
    void send(uint8_t recipient, const uint8_t* frame, uint8_t size);
    bool receive(
        uint32_t timeoutMs, uint8_t expectedSender, uint8_t* output,
        uint8_t expectedSize);
    // The first frame from the sender that fits; zero after the timeout.
    uint8_t receiveFrame(
        uint32_t timeoutMs, uint8_t expectedSender, uint8_t* output,
        uint8_t capacity);
    void sleep();

private:
    uint8_t receiveMatching(
        uint32_t timeoutMs, uint8_t expectedSender, uint8_t* output,
        uint8_t minimumSize, uint8_t maximumSize);

    RFM69 radio_;
};

}  // namespace node
}  // namespace radiosensors
