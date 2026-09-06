#include "CommissioningService.h"

#include <Arduino.h>
#include <string.h>

namespace radiosensors {
namespace node {

namespace {
constexpr uint32_t kAcceptWindowMs = 2500;
constexpr uint32_t kCompleteWindowMs = 1000;
constexpr uint8_t kConfirmAttempts = 3;
}

CommissioningService::CommissioningService(
    NodeRadio& radio, const uint16_t profileId,
    const protocol::FirmwareVersion firmware,
    const char* commissioningKey)
    : radio_(radio),
      store_(eeprom_),
      profileId_(profileId),
      firmware_(firmware),
      commissioningKey_(commissioningKey) {}

bool CommissioningService::begin() {
    readDeviceUid();
    const bool hasConfig = store_.load(config_);
#if defined(NODE_DEBUG)
    Serial.print(F("commissioning: EEPROM "));
    Serial.println(hasConfig ? F("configuration found") : F("unconfigured"));
#endif
    const uint8_t nodeId = hasConfig ? config_.nodeId : 0;
    const uint8_t networkId = hasConfig ? config_.networkId : 0;
    if (!radio_.begin(nodeId, networkId)) return false;
    if (hasConfig) {
        radio_.useOperationalProfile(config_);
    } else {
        radio_.useCommissioningProfile(commissioningKey_);
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
    Serial.println(F("commissioning: sending JOIN_REQUEST"));
#endif
    radio_.useCommissioningProfile(commissioningKey_);
    protocol::JoinRequest request{};
    memcpy(request.deviceUid, deviceUid_, sizeof(deviceUid_));
    request.profileId = profileId_;
    request.firmware = firmware_;
    request.requestNonce = createNonce();
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
        Serial.println(F("commissioning: JOIN_ACCEPT timeout"));
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
        Serial.println(F("commissioning: invalid JOIN_ACCEPT"));
#endif
        radio_.sleep();
        return false;
    }

    config_.state = storage::ProvisioningState::Provisional;
    config_.powerLevel = NODE_DEFAULT_POWER_LEVEL;
    config_.nodeId = accept.assignedNodeId;
    config_.gatewayId = accept.gatewayNodeId;
    config_.networkId = accept.networkId;
    memcpy(
        config_.installationKey, accept.installationKey,
        sizeof(config_.installationKey));
    config_.requestNonce = accept.requestNonce;
    config_.lastPowerCommandId = 0;
    if (!store_.save(config_)) {
#if defined(NODE_DEBUG)
        Serial.println(F("commissioning: provisional EEPROM save failed"));
#endif
        radio_.sleep();
        return false;
    }
#if defined(NODE_DEBUG)
    Serial.print(F("commissioning: JOIN_ACCEPT node="));
    Serial.println(config_.nodeId);
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
        Serial.print(F("commissioning: sending JOIN_CONFIRM attempt "));
        Serial.println(attempt + 1U);
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
            Serial.println(saved
                ? F("commissioning: active")
                : F("commissioning: active EEPROM save failed"));
#endif
            radio_.sleep();
            return saved;
        }
    }
#if defined(NODE_DEBUG)
    Serial.println(F("commissioning: JOIN_COMPLETE timeout"));
#endif
    radio_.sleep();
    return false;
}

}  // namespace node
}  // namespace radiosensors
