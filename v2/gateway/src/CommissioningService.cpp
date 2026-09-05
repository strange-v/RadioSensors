#include "CommissioningService.h"

#include <CommissioningFrames.h>
#include <JoinRequest.h>

#include "GatewayStatus.h"
#include "NodeRegistryStore.h"
#include "RadioConfig.h"
#include "RadioService.h"

namespace gateway::commissioning {
namespace {

// Registry transactions serialize and verify a complete 1360-byte snapshot.
constexpr uint32_t kTaskStackSize = 8192;
constexpr UBaseType_t kTaskPriority = 6;
constexpr BaseType_t kTaskCore = 1;
constexpr uint32_t kConfirmTimeoutMs = 5000;

Snapshot counters{};
portMUX_TYPE countersMux = portMUX_INITIALIZER_UNLOCKED;
bool awaitingConfirm = false;
uint32_t confirmDeadline = 0;

void increment(uint32_t Snapshot::*field) {
    portENTER_CRITICAL(&countersMux);
    ++(counters.*field);
    portEXIT_CRITICAL(&countersMux);
}

void returnToPairingIfOpen() {
    awaitingConfirm = false;
    confirmDeadline = 0;
    if (status::pairingActive()) {
        radio::requestProfile(radio::Profile::Commissioning);
        status::indicate(status::Indication::Pairing);
    }
}

void handleJoinRequest(const radio::ReceivedFrame& frame) {
    increment(&Snapshot::joinRequests);
    if (!status::pairingActive() || frame.senderId != 0 || awaitingConfirm) {
        increment(&Snapshot::rejectedFrames);
        return;
    }

    radiosensors::protocol::JoinRequest request{};
    if (radiosensors::protocol::decodeJoinRequest(
            frame.data, frame.size, request) !=
        radiosensors::protocol::JoinRequestStatus::Ok) {
        increment(&Snapshot::rejectedFrames);
        status::indicate(status::Indication::Error, 3000);
        return;
    }

    status::indicate(status::Indication::PersistingNode);
    radiosensors::registry::ReserveResult reserveResult{};
    const RegistryCommitStatus commit =
        registry_store::reserveAndSave(request, reserveResult);
    if (commit == RegistryCommitStatus::StorageError ||
        commit == RegistryCommitStatus::NotInitialized) {
        increment(&Snapshot::storageErrors);
        status::indicate(status::Indication::Error, 3000);
        return;
    }
    if (reserveResult.status != radiosensors::registry::ReserveStatus::Created &&
        reserveResult.status !=
            radiosensors::registry::ReserveStatus::ExistingPendingUpdated) {
        increment(&Snapshot::rejectedFrames);
        status::indicate(
            reserveResult.status == radiosensors::registry::ReserveStatus::ProfileConflict
                ? status::Indication::ProfileConflict
                : status::Indication::Error,
            3000);
        return;
    }

    radiosensors::protocol::JoinAccept accept{};
    for (size_t i = 0; i < radiosensors::protocol::kDeviceUidSize; ++i) {
        accept.deviceUid[i] = request.deviceUid[i];
    }
    accept.requestNonce = request.requestNonce;
    accept.assignedNodeId = reserveResult.nodeId;
    accept.gatewayNodeId = static_cast<uint8_t>(radio::config::nodeId);
    accept.networkId = radio::config::networkId;
    for (size_t i = 0; i < radiosensors::protocol::kInstallationKeySize; ++i) {
        accept.installationKey[i] =
            static_cast<uint8_t>(radio::config::encryptionKey[i]);
    }

    uint8_t encoded[radiosensors::protocol::kJoinAcceptSize];
    if (radiosensors::protocol::encodeJoinAccept(
            accept, encoded, sizeof(encoded)) !=
            radiosensors::protocol::CommissioningCodecStatus::Ok ||
        !radio::sendThenSwitchProfile(
            0, encoded, sizeof(encoded), radio::Profile::Operational)) {
        increment(&Snapshot::rejectedFrames);
        status::indicate(status::Indication::Error, 3000);
        return;
    }

    increment(&Snapshot::joinAcceptsQueued);
    awaitingConfirm = true;
    confirmDeadline = millis() + kConfirmTimeoutMs;
    status::indicate(status::Indication::AwaitingConfirm);
}

void handleJoinConfirm(const radio::ReceivedFrame& frame) {
    increment(&Snapshot::joinConfirms);
    radiosensors::protocol::JoinConfirm confirm{};
    if (radiosensors::protocol::decodeJoinConfirm(
            frame.data, frame.size, confirm) !=
            radiosensors::protocol::CommissioningCodecStatus::Ok) {
        increment(&Snapshot::rejectedFrames);
        return;
    }

    radiosensors::registry::ConfirmStatus result{};
    const RegistryCommitStatus commit = registry_store::confirmAndSave(
        confirm.deviceUid, static_cast<uint8_t>(frame.senderId),
        confirm.requestNonce, result);
    const bool newlyConfirmed =
        commit == RegistryCommitStatus::Ok &&
        result == radiosensors::registry::ConfirmStatus::Confirmed;
    const bool alreadyConfirmed =
        commit == RegistryCommitStatus::NoChange &&
        result == radiosensors::registry::ConfirmStatus::AlreadyActive;
    if (!newlyConfirmed && !alreadyConfirmed) {
        if (commit == RegistryCommitStatus::StorageError) {
            increment(&Snapshot::storageErrors);
        } else {
            increment(&Snapshot::rejectedFrames);
        }
        status::indicate(status::Indication::Error, 3000);
        return;
    }

    radiosensors::protocol::JoinComplete complete{};
    complete = confirm;
    uint8_t encoded[radiosensors::protocol::kJoinCompleteSize];
    if (radiosensors::protocol::encodeJoinComplete(
            complete, encoded, sizeof(encoded)) !=
            radiosensors::protocol::CommissioningCodecStatus::Ok ||
        !radio::send(static_cast<uint8_t>(frame.senderId), encoded,
                     sizeof(encoded))) {
        increment(&Snapshot::rejectedFrames);
        status::indicate(status::Indication::Error, 3000);
        return;
    }

    increment(&Snapshot::joinCompletesQueued);
    if (newlyConfirmed) increment(&Snapshot::nodesActivated);
    awaitingConfirm = false;
    confirmDeadline = 0;
    if (newlyConfirmed) {
        status::closePairing();
        status::indicate(status::Indication::PairingSucceeded, 1000);
    }
}

void task(void*) {
    for (;;) {
        radio::ReceivedFrame received{};
        if (radio::receive(received, pdMS_TO_TICKS(100))) {
            radiosensors::protocol::FrameView frame{};
            if (radiosensors::protocol::decodeFrame(
                    received.data, received.size, frame) ==
                radiosensors::protocol::DecodeStatus::Ok) {
                if (frame.kind == radiosensors::protocol::FrameKind::JoinRequest) {
                    handleJoinRequest(received);
                } else if (frame.kind == radiosensors::protocol::FrameKind::JoinConfirm) {
                    handleJoinConfirm(received);
                }
            }
        }
        if (awaitingConfirm &&
            static_cast<int32_t>(millis() - confirmDeadline) >= 0) {
            increment(&Snapshot::confirmTimeouts);
            returnToPairingIfOpen();
        }
    }
}

}  // namespace

bool begin() {
    if (xTaskCreatePinnedToCore(
            task, "commissioning", kTaskStackSize, nullptr, kTaskPriority,
            nullptr, kTaskCore) != pdPASS) {
        Serial.println("Commissioning task creation failed");
        return false;
    }
    Serial.println("Commissioning service ready");
    return true;
}

Snapshot snapshot() {
    portENTER_CRITICAL(&countersMux);
    const Snapshot value = counters;
    portEXIT_CRITICAL(&countersMux);
    return value;
}

}  // namespace gateway::commissioning
