#pragma once

#include <stddef.h>
#include <stdint.h>

#include "NodeRegistry.h"

namespace radiosensors {
namespace registry {

constexpr uint16_t kRegistryStorageVersion = 1;
constexpr size_t kRegistryHeaderSize = 12;
constexpr size_t kStoredNodeRecordSize = 21;
constexpr size_t kRegistryCrcSize = 4;
constexpr size_t kMaxRegistrySnapshotSize =
    kRegistryHeaderSize + kMaxNodes * kStoredNodeRecordSize + kRegistryCrcSize;

enum class SnapshotStatus : uint8_t {
    Ok,
    OutputTooSmall,
    InvalidSize,
    InvalidMagic,
    UnsupportedVersion,
    InvalidRecordCount,
    CrcMismatch,
    InvalidRegistry,
};

SnapshotStatus encodeRegistrySnapshot(
    const NodeRegistry& registry,
    uint32_t generation,
    uint8_t* output,
    size_t outputSize,
    size_t& encodedSize);

SnapshotStatus decodeRegistrySnapshot(
    const uint8_t* data,
    size_t size,
    NodeRegistry& registry,
    uint32_t& generation);

class RegistrySlotStorage {
public:
    virtual ~RegistrySlotStorage() {}
    virtual bool read(
        uint8_t slot,
        uint8_t* output,
        size_t capacity,
        size_t& size) = 0;
    virtual bool write(uint8_t slot, const uint8_t* data, size_t size) = 0;
};

enum class LoadStatus : uint8_t {
    Loaded,
    Empty,
};

class DualSlotRegistryStore {
public:
    explicit DualSlotRegistryStore(RegistrySlotStorage& storage);

    LoadStatus load(NodeRegistry& registry);
    bool save(const NodeRegistry& registry);
    uint32_t generation() const;

private:
    RegistrySlotStorage& storage_;
    uint32_t generation_;
    int8_t activeSlot_;
};

}  // namespace registry
}  // namespace radiosensors
