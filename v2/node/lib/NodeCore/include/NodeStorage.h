#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace radiosensors {
namespace node {
namespace storage {

constexpr size_t kEepromSize = 256;
constexpr size_t kFactoryCredentialSize = 32;
constexpr size_t kFactoryKeySize = 16;
constexpr size_t kNetworkConfigSlotSize = 32;
constexpr size_t kNetworkConfigSlotA = 0x00;
constexpr size_t kNetworkConfigSlotB = 0x20;
constexpr size_t kSetCountSlotSize = 16;
constexpr size_t kSetCountSlotA = 0x40;
constexpr size_t kSetCountSlotB = 0x50;
constexpr size_t kCounterRingStart = 0x60;
constexpr size_t kCounterRecordSize = 5;
constexpr size_t kCounterRecordCount = 32;
static_assert(
    kCounterRingStart + kCounterRecordSize * kCounterRecordCount == kEepromSize,
    "counter EEPROM layout must fill the ATtiny1614 EEPROM exactly");

enum class ProvisioningState : uint8_t {
    Provisional = 1,
    Active = 2,
};

struct NetworkConfig {
    uint8_t generation = 0;
    ProvisioningState state = ProvisioningState::Provisional;
    uint8_t powerLevel = 0;
    uint8_t nodeId = 0;
    uint8_t gatewayId = 0;
    uint8_t networkId = 0;
    uint8_t installationKey[16]{};
    uint32_t requestNonce = 0;
    uint16_t lastPowerCommandId = 0;
};

struct FactoryCredentials {
    uint8_t key[kFactoryKeySize]{};
};

enum class SetCountStatus : uint8_t {
    Pending = 1,
    Applied = 2,
};

struct SetCountResult {
    uint8_t generation = 0;
    SetCountStatus status = SetCountStatus::Pending;
    uint16_t commandId = 0;
    uint32_t oldCount = 0;
    uint32_t appliedCount = 0;
};

inline uint16_t crc16Ccitt(const uint8_t* data, size_t size) {
    uint16_t crc = 0xFFFFU;
    while (size-- != 0) {
        crc ^= static_cast<uint16_t>(*data++) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

inline void write16(uint8_t* output, const uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

inline void write32(uint8_t* output, const uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

inline uint16_t read16(const uint8_t* input) {
    return static_cast<uint16_t>(input[0]) |
        (static_cast<uint16_t>(input[1]) << 8);
}

inline uint32_t read32(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

inline bool encodeFactoryCredentials(
    const FactoryCredentials& value, uint8_t* output,
    const size_t capacity) {
    if (output == nullptr || capacity < kFactoryCredentialSize) return false;
    uint8_t combined = 0;
    for (size_t index = 0; index < sizeof(value.key); ++index)
        combined |= value.key[index];
    if (combined == 0) return false;
    memset(output, 0, kFactoryCredentialSize);
    output[0] = 'R';
    output[1] = 'S';
    output[2] = 'F';
    output[3] = 'C';
    output[4] = 1;
    output[5] = 1;
    memcpy(output + 8, value.key, sizeof(value.key));
    write16(output + 24, crc16Ccitt(output, 24));
    output[26] = static_cast<uint8_t>(~output[24]);
    output[27] = static_cast<uint8_t>(~output[25]);
    return true;
}

inline bool decodeFactoryCredentials(
    const uint8_t* input, const size_t size, FactoryCredentials& value) {
    if (input == nullptr || size != kFactoryCredentialSize ||
        input[0] != 'R' || input[1] != 'S' || input[2] != 'F' ||
        input[3] != 'C' || input[4] != 1 || input[5] != 1 ||
        input[6] != 0 || input[7] != 0 ||
        input[26] != static_cast<uint8_t>(~input[24]) ||
        input[27] != static_cast<uint8_t>(~input[25]) ||
        read16(input + 24) != crc16Ccitt(input, 24)) return false;
    for (size_t index = 28; index < kFactoryCredentialSize; ++index)
        if (input[index] != 0) return false;
    uint8_t combined = 0;
    for (size_t index = 0; index < kFactoryKeySize; ++index)
        combined |= input[8 + index];
    if (combined == 0) return false;
    memcpy(value.key, input + 8, sizeof(value.key));
    return true;
}

template <typename Storage>
class FactoryCredentialStore {
public:
    explicit FactoryCredentialStore(Storage& storage) : storage_(storage) {}

    bool load(FactoryCredentials& value) const {
        uint8_t bytes[kFactoryCredentialSize];
        for (size_t index = 0; index < sizeof(bytes); ++index)
            bytes[index] = storage_.read(index);
        return decodeFactoryCredentials(bytes, sizeof(bytes), value);
    }

private:
    Storage& storage_;
};

inline bool encodeNetworkConfig(
    const NetworkConfig& value, uint8_t* output, const size_t capacity) {
    constexpr uint8_t kMagic0 = 'R';
    constexpr uint8_t kMagic1 = 'N';
    constexpr uint8_t kSchema = 1;
    if (output == nullptr || capacity < kNetworkConfigSlotSize ||
        value.powerLevel > 31 || value.nodeId == 0 || value.nodeId == 255 ||
        value.gatewayId == 0 || value.gatewayId == 255 ||
        value.gatewayId == value.nodeId) {
        return false;
    }
    const uint8_t state = static_cast<uint8_t>(value.state);
    if (state != static_cast<uint8_t>(ProvisioningState::Provisional) &&
        state != static_cast<uint8_t>(ProvisioningState::Active)) {
        return false;
    }

    memset(output, 0, kNetworkConfigSlotSize);
    output[0] = kMagic0;
    output[1] = kMagic1;
    output[2] = kSchema;
    output[3] = value.generation;
    output[4] = static_cast<uint8_t>((state << 5) | value.powerLevel);
    output[5] = value.nodeId;
    output[6] = value.gatewayId;
    output[7] = value.networkId;
    memcpy(output + 8, value.installationKey, sizeof(value.installationKey));
    write32(output + 24, value.requestNonce);
    write16(output + 28, value.lastPowerCommandId);
    write16(output + 30, crc16Ccitt(output, 30));
    return true;
}

inline bool decodeNetworkConfig(
    const uint8_t* input, const size_t size, NetworkConfig& value) {
    if (input == nullptr || size != kNetworkConfigSlotSize ||
        input[0] != 'R' || input[1] != 'N' || input[2] != 1 ||
        read16(input + 30) != crc16Ccitt(input, 30)) {
        return false;
    }
    const uint8_t state = input[4] >> 5;
    const uint8_t powerLevel = input[4] & 0x1FU;
    if ((state != static_cast<uint8_t>(ProvisioningState::Provisional) &&
         state != static_cast<uint8_t>(ProvisioningState::Active)) ||
        input[5] == 0 || input[5] == 255 || input[6] == 0 ||
        input[6] == 255 || input[5] == input[6]) {
        return false;
    }
    value.generation = input[3];
    value.state = static_cast<ProvisioningState>(state);
    value.powerLevel = powerLevel;
    value.nodeId = input[5];
    value.gatewayId = input[6];
    value.networkId = input[7];
    memcpy(value.installationKey, input + 8, sizeof(value.installationKey));
    value.requestNonce = read32(input + 24);
    value.lastPowerCommandId = read16(input + 28);
    return true;
}

inline bool encodeSetCountResult(
    const SetCountResult& value, uint8_t* output, const size_t capacity) {
    if (output == nullptr || capacity < kSetCountSlotSize) return false;
    const uint8_t status = static_cast<uint8_t>(value.status);
    if (status != static_cast<uint8_t>(SetCountStatus::Pending) &&
        status != static_cast<uint8_t>(SetCountStatus::Applied)) {
        return false;
    }
    memset(output, 0, kSetCountSlotSize);
    output[0] = 0xC7;
    output[1] = 1;
    output[2] = value.generation;
    output[3] = status;
    write16(output + 4, value.commandId);
    write32(output + 6, value.oldCount);
    write32(output + 10, value.appliedCount);
    write16(output + 14, crc16Ccitt(output, 14));
    return true;
}

inline bool decodeSetCountResult(
    const uint8_t* input, const size_t size, SetCountResult& value) {
    if (input == nullptr || size != kSetCountSlotSize || input[0] != 0xC7 ||
        input[1] != 1 || read16(input + 14) != crc16Ccitt(input, 14)) {
        return false;
    }
    if (input[3] != static_cast<uint8_t>(SetCountStatus::Pending) &&
        input[3] != static_cast<uint8_t>(SetCountStatus::Applied)) {
        return false;
    }
    value.generation = input[2];
    value.status = static_cast<SetCountStatus>(input[3]);
    value.commandId = read16(input + 4);
    value.oldCount = read32(input + 6);
    value.appliedCount = read32(input + 10);
    return true;
}

inline bool generationIsNewer(const uint8_t candidate, const uint8_t current) {
    return static_cast<int8_t>(candidate - current) > 0;
}

template <typename Storage>
inline void readBlock(
    Storage& storage, const size_t address, uint8_t* output,
    const size_t size) {
    for (size_t index = 0; index < size; ++index) {
        output[index] = storage.read(address + index);
    }
}

template <typename Storage>
class NetworkConfigStore {
public:
    explicit NetworkConfigStore(Storage& storage) : storage_(storage) {}

    bool load(NetworkConfig& value) {
        uint8_t first[kNetworkConfigSlotSize];
        uint8_t second[kNetworkConfigSlotSize];
        NetworkConfig a{};
        NetworkConfig b{};
        readBlock(storage_, kNetworkConfigSlotA, first, sizeof(first));
        readBlock(storage_, kNetworkConfigSlotB, second, sizeof(second));
        const bool validA = decodeNetworkConfig(first, sizeof(first), a);
        const bool validB = decodeNetworkConfig(second, sizeof(second), b);
        initialized_ = true;
        hasValue_ = validA || validB;
        if (!hasValue_) {
            currentSlot_ = 1;
            currentGeneration_ = 0;
            return false;
        }
        if (validA && (!validB || generationIsNewer(a.generation, b.generation))) {
            value = a;
            currentSlot_ = 0;
        } else {
            value = b;
            currentSlot_ = 1;
        }
        currentGeneration_ = value.generation;
        return true;
    }

    bool save(NetworkConfig& value) {
        if (!initialized_) {
            NetworkConfig ignored{};
            load(ignored);
        }
        value.generation = hasValue_
            ? static_cast<uint8_t>(currentGeneration_ + 1U)
            : 0;
        uint8_t bytes[kNetworkConfigSlotSize];
        if (!encodeNetworkConfig(value, bytes, sizeof(bytes))) return false;
        const uint8_t nextSlot = static_cast<uint8_t>(currentSlot_ ^ 1U);
        const size_t address = nextSlot == 0
            ? kNetworkConfigSlotA
            : kNetworkConfigSlotB;
        storage_.update(address, 0);
        storage_.update(address + 1, 0);
        for (size_t index = 2; index < sizeof(bytes); ++index) {
            storage_.update(address + index, bytes[index]);
        }
        storage_.update(address + 1, bytes[1]);
        storage_.update(address, bytes[0]);
        currentSlot_ = nextSlot;
        currentGeneration_ = value.generation;
        hasValue_ = true;
        return true;
    }

    void factoryReset() {
        storage_.update(kNetworkConfigSlotA, 0);
        storage_.update(kNetworkConfigSlotA + 1, 0);
        storage_.update(kNetworkConfigSlotB, 0);
        storage_.update(kNetworkConfigSlotB + 1, 0);
        initialized_ = true;
        hasValue_ = false;
        currentSlot_ = 1;
        currentGeneration_ = 0;
    }

private:
    Storage& storage_;
    bool initialized_ = false;
    bool hasValue_ = false;
    uint8_t currentSlot_ = 1;
    uint8_t currentGeneration_ = 0;
};

template <typename Storage>
class SetCountStore {
public:
    explicit SetCountStore(Storage& storage) : storage_(storage) {}

    bool load(SetCountResult& value) {
        uint8_t first[kSetCountSlotSize];
        uint8_t second[kSetCountSlotSize];
        SetCountResult a{};
        SetCountResult b{};
        readBlock(storage_, kSetCountSlotA, first, sizeof(first));
        readBlock(storage_, kSetCountSlotB, second, sizeof(second));
        const bool validA = decodeSetCountResult(first, sizeof(first), a);
        const bool validB = decodeSetCountResult(second, sizeof(second), b);
        initialized_ = true;
        hasValue_ = validA || validB;
        if (!hasValue_) {
            currentSlot_ = 1;
            currentGeneration_ = 0;
            return false;
        }
        if (validA && (!validB || generationIsNewer(a.generation, b.generation))) {
            value = a;
            currentSlot_ = 0;
        } else {
            value = b;
            currentSlot_ = 1;
        }
        currentGeneration_ = value.generation;
        return true;
    }

    bool save(SetCountResult& value) {
        if (!initialized_) {
            SetCountResult ignored{};
            load(ignored);
        }
        value.generation = hasValue_
            ? static_cast<uint8_t>(currentGeneration_ + 1U)
            : 0;
        uint8_t bytes[kSetCountSlotSize];
        if (!encodeSetCountResult(value, bytes, sizeof(bytes))) return false;
        const uint8_t nextSlot = static_cast<uint8_t>(currentSlot_ ^ 1U);
        const size_t address = nextSlot == 0 ? kSetCountSlotA : kSetCountSlotB;
        storage_.update(address, 0);
        for (size_t index = 1; index < sizeof(bytes); ++index) {
            storage_.update(address + index, bytes[index]);
        }
        storage_.update(address, bytes[0]);
        currentSlot_ = nextSlot;
        currentGeneration_ = value.generation;
        hasValue_ = true;
        return true;
    }

private:
    Storage& storage_;
    bool initialized_ = false;
    bool hasValue_ = false;
    uint8_t currentSlot_ = 1;
    uint8_t currentGeneration_ = 0;
};

inline bool counterSequenceIsNewer(
    const uint8_t candidate, const uint8_t current) {
    const uint16_t delta = candidate >= current
        ? candidate - current
        : static_cast<uint16_t>(candidate) + 255U - current;
    return delta != 0 && delta < 128U;
}

template <typename Storage>
class CounterStore {
public:
    explicit CounterStore(Storage& storage) : storage_(storage) {}

    bool load(uint32_t& count) {
        bool found = false;
        uint8_t newestSequence = 0;
        uint8_t newestSlot = 0;
        uint32_t newestCount = 0;
        for (uint8_t slot = 0; slot < kCounterRecordCount; ++slot) {
            const size_t address = kCounterRingStart + slot * kCounterRecordSize;
            const uint8_t sequence = storage_.read(address);
            if (sequence == 0xFFU) continue;
            uint8_t bytes[4];
            readBlock(storage_, address + 1, bytes, sizeof(bytes));
            if (!found || counterSequenceIsNewer(sequence, newestSequence)) {
                found = true;
                newestSequence = sequence;
                newestSlot = slot;
                newestCount = read32(bytes);
            }
        }
        initialized_ = true;
        hasValue_ = found;
        if (!found) {
            currentSlot_ = static_cast<uint8_t>(kCounterRecordCount - 1);
            currentSequence_ = 0;
            currentCount_ = 0;
            count = 0;
            return false;
        }
        currentSlot_ = newestSlot;
        currentSequence_ = newestSequence;
        currentCount_ = newestCount;
        count = newestCount;
        return true;
    }

    bool save(const uint32_t count) {
        if (!initialized_) {
            uint32_t ignored = 0;
            load(ignored);
        }
        if (hasValue_ && count == currentCount_) return true;
        const uint8_t nextSlot = static_cast<uint8_t>(
            (currentSlot_ + 1U) % kCounterRecordCount);
        const uint8_t nextSequence = !hasValue_
            ? 0
            : (currentSequence_ == 0xFEU
                ? 0
                : static_cast<uint8_t>(currentSequence_ + 1U));
        const size_t address =
            kCounterRingStart + nextSlot * kCounterRecordSize;
        uint8_t bytes[4];
        write32(bytes, count);
        storage_.update(address, 0xFFU);
        for (uint8_t index = 0; index < sizeof(bytes); ++index) {
            storage_.update(address + 1U + index, bytes[index]);
        }
        storage_.update(address, nextSequence);
        currentSlot_ = nextSlot;
        currentSequence_ = nextSequence;
        currentCount_ = count;
        hasValue_ = true;
        return true;
    }

private:
    Storage& storage_;
    bool initialized_ = false;
    bool hasValue_ = false;
    uint8_t currentSlot_ = static_cast<uint8_t>(kCounterRecordCount - 1);
    uint8_t currentSequence_ = 0;
    uint32_t currentCount_ = 0;
};

}  // namespace storage
}  // namespace node
}  // namespace radiosensors
