#pragma once

#include <NodeRegistry.h>

enum class RegistryCommitStatus {
    Ok,
    NoChange,
    StorageError,
    NotInitialized,
};

namespace gateway::registry_store {

bool begin();
uint32_t generation();
size_t recordCount();
bool isActiveNode(uint8_t nodeId);
bool activeProfileId(uint8_t nodeId, uint16_t& profileId);
RegistryCommitStatus reserveAndSave(
    const radiosensors::protocol::JoinRequest& request,
    radiosensors::registry::ReserveResult& result);
RegistryCommitStatus confirmAndSave(
    const uint8_t* deviceUid,
    uint8_t nodeId,
    uint32_t nonce,
    radiosensors::registry::ConfirmStatus& result);

}  // namespace gateway::registry_store
