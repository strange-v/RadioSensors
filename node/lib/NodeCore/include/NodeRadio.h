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
    void useCommissioningProfile(const uint8_t (&factoryKey)[16]);
    void useOperationalProfile(const storage::NetworkConfig& config);
    void setPowerLevel(uint8_t level);
    // Rewrites the applied profile when the module has lost its registers,
    // for instance after its own brown-out. Returns whether the module holds
    // that profile afterwards; call it before each exchange.
    bool ensureConfigured();
    // Up to `attempts` transmissions. `ack` is what the acknowledgement
    // carried, `downlinkRssi` its strength or protocol::kNoDownlinkRssi.
    bool sendTelemetry(
        uint8_t gatewayId, const uint8_t* frame, uint8_t size, uint8_t attempts,
        protocol::TelemetryAck& ack, int8_t& downlinkRssi);
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
    void applyProfile(
        uint8_t address, uint8_t network, const uint8_t (&key)[16]);
    bool configured();

    RFM69 radio_;
    // The applied profile, restored by ensureConfigured(). The AES key
    // registers read back as zero, so the key itself cannot be compared.
    uint8_t address_ = 0;
    uint8_t network_ = 0;
    uint8_t key_[16]{};
    uint8_t level_ = NODE_RADIO_MAX_POWER_LEVEL;
    uint8_t frequencyMsb_ = 0;
};

}  // namespace node
}  // namespace radiosensors
