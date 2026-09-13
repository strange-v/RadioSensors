#pragma once

#include <CommandSessionFrames.h>
#include <CommissioningFrames.h>
#include <JoinRequest.h>
#include <stdint.h>

#include "ArduinoEepromStorage.h"
#include "NodeRadio.h"
#include "NodeStorage.h"
#include "UserRowStorage.h"

namespace radiosensors {
namespace node {

class CommissioningService {
public:
    CommissioningService(
        NodeRadio& radio, uint16_t profileId,
        protocol::FirmwareVersion firmware);

    bool begin();
    bool active() const;
    bool advance();
    const storage::NetworkConfig& config() const;

    // Invalidates both network configuration slots. Refused without valid
    // factory credentials, because the node could then never rejoin.
    bool resetNetwork();

    uint32_t createNonce() const;

    // Stores the level with the network configuration. The radio keeps its
    // current level until applyRadioProfile(), so the command result still
    // goes out at the level the gateway last heard.
    protocol::CommandStatus storeRadioPower(uint16_t commandId, uint8_t level);
    void applyRadioProfile();

private:
    void readDeviceUid();
    bool requestJoin();
    bool confirmJoin();

    NodeRadio& radio_;
    storage::ArduinoEepromStorage eeprom_;
    storage::UserRowStorage userRow_;
    storage::FactoryCredentialStore<storage::UserRowStorage> factoryStore_;
    storage::NetworkConfigStore<storage::ArduinoEepromStorage> store_;
    storage::NetworkConfig config_{};
    uint8_t deviceUid_[protocol::kDeviceUidSize]{};
    uint16_t profileId_;
    protocol::FirmwareVersion firmware_;
    storage::FactoryCredentials factoryCredentials_{};
    bool factoryCredentialsValid_ = false;
};

}  // namespace node
}  // namespace radiosensors
