#include "RegistryPersistence.h"

#include "JoinRequest.h"

namespace radiosensors {
namespace registry {
namespace {

constexpr uint8_t kMagic[4] = {'R', 'S', 'N', 'R'};

uint32_t crc32(const uint8_t* data, const size_t size) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

bool isNewerGeneration(const uint32_t candidate, const uint32_t current) {
    return static_cast<int32_t>(candidate - current) > 0;
}

}  // namespace

SnapshotStatus encodeRegistrySnapshot(
    const NodeRegistry& registry,
    const uint32_t generation,
    uint8_t* const output,
    const size_t outputSize,
    size_t& encodedSize) {
    const size_t requiredSize = kRegistryHeaderSize +
        registry.size() * kStoredNodeRecordSize + kRegistryCrcSize;
    encodedSize = 0;
    if (output == nullptr || outputSize < requiredSize) {
        return SnapshotStatus::OutputTooSmall;
    }

    output[0] = kMagic[0];
    output[1] = kMagic[1];
    output[2] = kMagic[2];
    output[3] = kMagic[3];
    protocol::writeUint16Le(output + 4, kRegistryStorageVersion);
    protocol::writeUint32Le(output + 6, generation);
    output[10] = static_cast<uint8_t>(registry.size());
    output[11] = 0;

    size_t offset = kRegistryHeaderSize;
    const NodeRecord* const records = registry.records();
    for (size_t index = 0; index < registry.size(); ++index) {
        const NodeRecord& record = records[index];
        for (size_t uidIndex = 0; uidIndex < protocol::kDeviceUidSize; ++uidIndex) {
            output[offset + uidIndex] = record.deviceUid[uidIndex];
        }
        output[offset + 10] = record.nodeId;
        protocol::writeUint16Le(output + offset + 11, record.profileId);
        output[offset + 13] = record.firmware.major;
        output[offset + 14] = record.firmware.minor;
        output[offset + 15] = record.firmware.patch;
        output[offset + 16] = static_cast<uint8_t>(record.state);
        protocol::writeUint32Le(output + offset + 17, record.requestNonce);
        offset += kStoredNodeRecordSize;
    }

    protocol::writeUint32Le(output + offset, crc32(output, offset));
    encodedSize = requiredSize;
    return SnapshotStatus::Ok;
}

SnapshotStatus decodeRegistrySnapshot(
    const uint8_t* const data,
    const size_t size,
    NodeRegistry& registry,
    uint32_t& generation) {
    if (data == nullptr || size < kRegistryHeaderSize + kRegistryCrcSize) {
        return SnapshotStatus::InvalidSize;
    }
    if (data[0] != kMagic[0] || data[1] != kMagic[1] ||
        data[2] != kMagic[2] || data[3] != kMagic[3]) {
        return SnapshotStatus::InvalidMagic;
    }
    if (protocol::readUint16Le(data + 4) != kRegistryStorageVersion) {
        return SnapshotStatus::UnsupportedVersion;
    }

    const size_t count = data[10];
    if (count > kMaxNodes) {
        return SnapshotStatus::InvalidRecordCount;
    }
    const size_t expectedSize = kRegistryHeaderSize +
        count * kStoredNodeRecordSize + kRegistryCrcSize;
    if (size != expectedSize) {
        return SnapshotStatus::InvalidSize;
    }
    const uint32_t storedCrc = protocol::readUint32Le(data + size - kRegistryCrcSize);
    if (storedCrc != crc32(data, size - kRegistryCrcSize)) {
        return SnapshotStatus::CrcMismatch;
    }

    NodeRecord records[kMaxNodes]{};
    size_t offset = kRegistryHeaderSize;
    for (size_t index = 0; index < count; ++index) {
        NodeRecord& record = records[index];
        for (size_t uidIndex = 0; uidIndex < protocol::kDeviceUidSize; ++uidIndex) {
            record.deviceUid[uidIndex] = data[offset + uidIndex];
        }
        record.nodeId = data[offset + 10];
        record.profileId = protocol::readUint16Le(data + offset + 11);
        record.firmware = protocol::FirmwareVersion{
            data[offset + 13], data[offset + 14], data[offset + 15]};
        record.state = static_cast<NodeState>(data[offset + 16]);
        record.requestNonce = protocol::readUint32Le(data + offset + 17);
        offset += kStoredNodeRecordSize;
    }

    if (!registry.restore(records, count)) {
        return SnapshotStatus::InvalidRegistry;
    }
    generation = protocol::readUint32Le(data + 6);
    return SnapshotStatus::Ok;
}

DualSlotRegistryStore::DualSlotRegistryStore(RegistrySlotStorage& storage)
    : storage_(storage), generation_(0), activeSlot_(-1) {}

LoadStatus DualSlotRegistryStore::load(NodeRegistry& registry) {
    uint8_t buffer[kMaxRegistrySnapshotSize];
    uint32_t newestGeneration = 0;
    int8_t newestSlot = -1;

    for (uint8_t slot = 0; slot < 2; ++slot) {
        size_t size = 0;
        if (!storage_.read(slot, buffer, sizeof(buffer), size)) {
            continue;
        }
        NodeRegistry candidate;
        uint32_t candidateGeneration = 0;
        if (decodeRegistrySnapshot(
                buffer, size, candidate, candidateGeneration) != SnapshotStatus::Ok) {
            continue;
        }
        if (newestSlot < 0 || isNewerGeneration(candidateGeneration, newestGeneration)) {
            registry = candidate;
            newestGeneration = candidateGeneration;
            newestSlot = static_cast<int8_t>(slot);
        }
    }

    if (newestSlot < 0) {
        registry = NodeRegistry{};
        generation_ = 0;
        activeSlot_ = -1;
        return LoadStatus::Empty;
    }

    generation_ = newestGeneration;
    activeSlot_ = newestSlot;
    return LoadStatus::Loaded;
}

bool DualSlotRegistryStore::save(const NodeRegistry& registry) {
    uint8_t encoded[kMaxRegistrySnapshotSize];
    size_t encodedSize = 0;
    const uint32_t nextGeneration = generation_ + 1;
    if (encodeRegistrySnapshot(
            registry,
            nextGeneration,
            encoded,
            sizeof(encoded),
            encodedSize) != SnapshotStatus::Ok) {
        return false;
    }

    const uint8_t targetSlot = activeSlot_ == 0 ? 1 : 0;
    if (!storage_.write(targetSlot, encoded, encodedSize)) {
        return false;
    }

    size_t verifySize = 0;
    NodeRegistry verified;
    uint32_t verifiedGeneration = 0;
    if (!storage_.read(targetSlot, encoded, sizeof(encoded), verifySize) ||
        decodeRegistrySnapshot(
            encoded,
            verifySize,
            verified,
            verifiedGeneration) != SnapshotStatus::Ok ||
        verifiedGeneration != nextGeneration) {
        return false;
    }

    generation_ = nextGeneration;
    activeSlot_ = static_cast<int8_t>(targetSlot);
    return true;
}

uint32_t DualSlotRegistryStore::generation() const {
    return generation_;
}

}  // namespace registry
}  // namespace radiosensors
