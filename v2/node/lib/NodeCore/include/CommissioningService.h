#pragma once

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

private:
    void readDeviceUid();
    uint32_t createNonce() const;
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
};

}  // namespace node
}  // namespace radiosensors
