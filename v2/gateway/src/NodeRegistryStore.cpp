#include "NodeRegistryStore.h"

#include <Preferences.h>
#include <RegistryPersistence.h>

namespace gateway::registry_store {
namespace {

constexpr const char* kNamespace = "node-reg";
constexpr const char* kSlotKeys[2] = {"registry_a", "registry_b"};

class NvsSlotStorage final : public radiosensors::registry::RegistrySlotStorage {
public:
    bool begin() {
        return preferences_.begin(kNamespace, false);
    }

    bool read(
        const uint8_t slot,
        uint8_t* const output,
        const size_t capacity,
        size_t& size) override {
        size = 0;
        if (slot >= 2) {
            return false;
        }
        const size_t storedSize = preferences_.getBytesLength(kSlotKeys[slot]);
        if (storedSize == 0 || storedSize > capacity) {
            return false;
        }
        const size_t bytesRead = preferences_.getBytes(
            kSlotKeys[slot], output, storedSize);
        if (bytesRead != storedSize) {
            return false;
        }
        size = bytesRead;
        return true;
    }

    bool write(
        const uint8_t slot,
        const uint8_t* const data,
        const size_t size) override {
        return slot < 2 && data != nullptr &&
               preferences_.putBytes(kSlotKeys[slot], data, size) == size;
    }

private:
    Preferences preferences_;
};

NvsSlotStorage storage;
radiosensors::registry::NodeRegistry nodes;
radiosensors::registry::DualSlotRegistryStore store(storage);
bool initialized = false;
SemaphoreHandle_t mutex = nullptr;

}  // namespace

bool begin() {
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr) {
        Serial.println("Node registry mutex creation failed");
        return false;
    }
    if (!storage.begin()) {
        Serial.println("Node registry NVS initialization failed");
        return false;
    }

    const radiosensors::registry::LoadStatus status = store.load(nodes);
    initialized = true;
    Serial.printf(
        "Node registry ready: records=%u generation=%lu source=%s\n",
        static_cast<unsigned>(nodes.size()),
        static_cast<unsigned long>(store.generation()),
        status == radiosensors::registry::LoadStatus::Loaded ? "nvs" : "empty");
    return true;
}

uint32_t generation() {
    if (mutex == nullptr || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return 0;
    const uint32_t value = store.generation();
    xSemaphoreGive(mutex);
    return value;
}

size_t recordCount() {
    if (mutex == nullptr || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return 0;
    const size_t value = nodes.size();
    xSemaphoreGive(mutex);
    return value;
}

RegistryCommitStatus reserveAndSave(
    const radiosensors::protocol::JoinRequest& request,
    radiosensors::registry::ReserveResult& result) {
    if (!initialized || mutex == nullptr) return RegistryCommitStatus::NotInitialized;
    xSemaphoreTake(mutex, portMAX_DELAY);
    radiosensors::registry::NodeRegistry candidate = nodes;
    result = candidate.reserve(request);
    const bool changed =
        result.status == radiosensors::registry::ReserveStatus::Created ||
        result.status == radiosensors::registry::ReserveStatus::ExistingPendingUpdated;
    if (!changed) {
        xSemaphoreGive(mutex);
        return RegistryCommitStatus::NoChange;
    }
    if (!store.save(candidate)) {
        xSemaphoreGive(mutex);
        return RegistryCommitStatus::StorageError;
    }
    nodes = candidate;
    xSemaphoreGive(mutex);
    return RegistryCommitStatus::Ok;
}

RegistryCommitStatus confirmAndSave(
    const uint8_t* deviceUid,
    const uint8_t nodeId,
    const uint32_t nonce,
    radiosensors::registry::ConfirmStatus& result) {
    if (!initialized || mutex == nullptr) return RegistryCommitStatus::NotInitialized;
    xSemaphoreTake(mutex, portMAX_DELAY);
    radiosensors::registry::NodeRegistry candidate = nodes;
    result = candidate.confirm(deviceUid, nodeId, nonce);
    if (result != radiosensors::registry::ConfirmStatus::Confirmed) {
        xSemaphoreGive(mutex);
        return RegistryCommitStatus::NoChange;
    }
    if (!store.save(candidate)) {
        xSemaphoreGive(mutex);
        return RegistryCommitStatus::StorageError;
    }
    nodes = candidate;
    xSemaphoreGive(mutex);
    return RegistryCommitStatus::Ok;
}

}  // namespace gateway::registry_store
