#include "CommissioningService.h"

#include <Arduino.h>
#include <string.h>

#include "DebugLog.h"

namespace radiosensors {
namespace node {

namespace {
constexpr uint32_t kAcceptWindowMs = 2500;
constexpr uint32_t kCompleteWindowMs = 1000;
constexpr uint8_t kConfirmAttempts = 3;
}

CommissioningService::CommissioningService(
    NodeRadio& radio, const uint16_t profileId,
    const protocol::FirmwareVersion firmware)
    : radio_(radio),
      factoryStore_(userRow_),
      store_(eeprom_),
      profileId_(profileId),
      firmware_(firmware) {}

bool CommissioningService::begin() {
    readDeviceUid();
    const bool hasConfig = store_.load(config_);
    factoryCredentialsValid_ = factoryStore_.load(factoryCredentials_);
    if (!hasConfig && !factoryCredentialsValid_) {
#if defined(NODE_DEBUG)
        debugLine(F("join no fcred"));
#endif
        return false;
    }
#if defined(NODE_DEBUG)
    debugLine(hasConfig ? F("cfg ok") : F("cfg none"));
#endif
    const uint8_t nodeId = hasConfig ? config_.nodeId : 0;
    const uint8_t networkId = hasConfig ? config_.networkId : 0;
    if (!radio_.begin(nodeId, networkId)) return false;
    if (hasConfig) {
        radio_.useOperationalProfile(config_);
    } else {
        radio_.useCommissioningProfile(factoryCredentials_.key);
    }
    radio_.sleep();
    return true;
}

bool CommissioningService::active() const {
    return config_.state == storage::ProvisioningState::Active &&
        config_.nodeId != 0;
}

const storage::NetworkConfig& CommissioningService::config() const {
    return config_;
}

bool CommissioningService::resetNetwork() {
    if (!factoryCredentialsValid_) return false;
    store_.factoryReset();
    config_ = storage::NetworkConfig{};
    return true;
}

void CommissioningService::readDeviceUid() {
    volatile const uint8_t* source = &SIGROW_SERNUM0;
    for (uint8_t index = 0; index < sizeof(deviceUid_); ++index) {
        deviceUid_[index] = source[index];
    }
}

uint32_t CommissioningService::createNonce() const {
    uint32_t value = micros() ^ 0xA5C31F27UL;
    for (uint8_t index = 0; index < sizeof(deviceUid_); ++index) {
        value = (value ^ deviceUid_[index]) * 16777619UL;
    }
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

bool CommissioningService::advance() {
    if (active()) return true;
    if (config_.nodeId != 0) return confirmJoin();
    return requestJoin();
}

bool CommissioningService::requestJoin() {
#if defined(NODE_DEBUG)
    debugLine(F("join req"));
#endif
    radio_.useCommissioningProfile(factoryCredentials_.key);
    protocol::JoinRequest request{};
    memcpy(request.deviceUid, deviceUid_, sizeof(deviceUid_));
    request.profileId = profileId_;
    request.firmware = firmware_;
    request.requestNonce = createNonce();
    request.maxPowerLevel = NODE_RADIO_MAX_POWER_LEVEL;
    uint8_t requestBytes[protocol::kJoinRequestSize];
    if (protocol::encodeJoinRequest(
            request, requestBytes, sizeof(requestBytes)) !=
        protocol::JoinRequestStatus::Ok) {
        radio_.sleep();
        return false;
    }
    radio_.send(NODE_GATEWAY_ID, requestBytes, sizeof(requestBytes));

    uint8_t acceptBytes[protocol::kJoinAcceptSize];
    if (!radio_.receive(
            kAcceptWindowMs, NODE_GATEWAY_ID, acceptBytes,
            sizeof(acceptBytes))) {
#if defined(NODE_DEBUG)
        debugLine(F("join acc timeout"));
#endif
        radio_.sleep();
        return false;
    }
    protocol::JoinAccept accept{};
    if (protocol::decodeJoinAccept(
            acceptBytes, sizeof(acceptBytes), accept) !=
            protocol::CommissioningCodecStatus::Ok ||
        memcmp(accept.deviceUid, deviceUid_, sizeof(deviceUid_)) != 0 ||
        accept.requestNonce != request.requestNonce ||
        accept.gatewayNodeId != NODE_GATEWAY_ID) {
#if defined(NODE_DEBUG)
        debugLine(F("join acc bad"));
#endif
        radio_.sleep();
        return false;
    }

    config_.state = storage::ProvisioningState::Provisional;
    config_.nodeId = accept.assignedNodeId;
    config_.gatewayId = accept.gatewayNodeId;
    config_.networkId = accept.networkId;
    memcpy(
        config_.installationKey, accept.installationKey,
        sizeof(config_.installationKey));
    config_.requestNonce = accept.requestNonce;
    if (!store_.save(config_)) {
#if defined(NODE_DEBUG)
        debugLine(F("join acc nosave"));
#endif
        radio_.sleep();
        return false;
    }
#if defined(NODE_DEBUG)
    debugValue(F("join acc node="), config_.nodeId);
#endif
    radio_.useOperationalProfile(config_);
    return confirmJoin();
}

bool CommissioningService::confirmJoin() {
    radio_.useOperationalProfile(config_);
    protocol::JoinConfirm confirm{};
    memcpy(confirm.deviceUid, deviceUid_, sizeof(deviceUid_));
    confirm.requestNonce = config_.requestNonce;
    uint8_t confirmBytes[protocol::kJoinConfirmSize];
    if (protocol::encodeJoinConfirm(
            confirm, confirmBytes, sizeof(confirmBytes)) !=
        protocol::CommissioningCodecStatus::Ok) {
        radio_.sleep();
        return false;
    }

    for (uint8_t attempt = 0; attempt < kConfirmAttempts; ++attempt) {
#if defined(NODE_DEBUG)
        debugValue(F("join cfm "), attempt + 1U);
#endif
        radio_.send(config_.gatewayId, confirmBytes, sizeof(confirmBytes));
        uint8_t completeBytes[protocol::kJoinCompleteSize];
        if (!radio_.receive(
                kCompleteWindowMs, config_.gatewayId, completeBytes,
                sizeof(completeBytes))) {
            continue;
        }
        protocol::JoinComplete complete{};
        if (protocol::decodeJoinComplete(
                completeBytes, sizeof(completeBytes), complete) ==
                protocol::CommissioningCodecStatus::Ok &&
            memcmp(complete.deviceUid, deviceUid_, sizeof(deviceUid_)) == 0 &&
            complete.requestNonce == config_.requestNonce) {
            config_.state = storage::ProvisioningState::Active;
            const bool saved = store_.save(config_);
#if defined(NODE_DEBUG)
            debugLine(saved ? F("join ok") : F("join ok nosave"));
#endif
            radio_.sleep();
            return saved;
        }
    }
#if defined(NODE_DEBUG)
    debugLine(F("join cmpl timeout"));
#endif
    radio_.sleep();
    return false;
}

}  // namespace node
}  // namespace radiosensors
