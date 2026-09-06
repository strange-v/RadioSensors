#pragma once

#include <RFM69.h>
#include <stddef.h>
#include <stdint.h>

#include "NodeStorage.h"

namespace radiosensors {
namespace node {

class NodeRadio {
public:
    NodeRadio(uint8_t chipSelect, uint8_t interruptPin);

    bool begin(uint8_t nodeId, uint8_t networkId);
    void useCommissioningProfile(const uint8_t commissioningKey[16]);
    void useOperationalProfile(const storage::NetworkConfig& config);
    bool sendTelemetry(
        uint8_t gatewayId, const uint8_t* frame, uint8_t size);
    void send(uint8_t recipient, const uint8_t* frame, uint8_t size);
    bool receive(
        uint32_t timeoutMs, uint8_t expectedSender, uint8_t* output,
        uint8_t expectedSize);
    void sleep();

private:
    RFM69 radio_;
};

}  // namespace node
}  // namespace radiosensors
