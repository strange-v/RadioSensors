#pragma once

#include <NodeRegistry.h>

enum class RegistryCommitStatus {
    Ok,
    NoChange,
    StorageError,
    NotInitialized,
};

namespace gateway::registry_store {

struct Snapshot {
    uint32_t generation;
    size_t count;
    radiosensors::registry::NodeRecord records[radiosensors::registry::kMaxNodes];
};

bool begin();
uint32_t generation();
size_t recordCount();
bool isActiveNode(uint8_t nodeId);
bool hasActiveNodes();
bool activeProfileId(uint8_t nodeId, uint16_t& profileId);
bool snapshot(Snapshot& value);
RegistryCommitStatus reserveAndSave(
    const radiosensors::protocol::JoinRequest& request,
    radiosensors::registry::ReserveResult& result);
RegistryCommitStatus confirmAndSave(
    const uint8_t* deviceUid,
    uint8_t nodeId,
    uint32_t nonce,
    radiosensors::registry::ConfirmStatus& result);

}  // namespace gateway::registry_store
