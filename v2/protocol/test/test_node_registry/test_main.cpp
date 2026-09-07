#include <JoinRequest.h>
#include <NodeRegistry.h>
#include <RegistryPersistence.h>
#include <unity.h>

using namespace radiosensors::protocol;
using namespace radiosensors::registry;

namespace {

JoinRequest makeRequest(
    const uint8_t uidSeed,
    const uint16_t profileId = 1,
    const uint32_t nonce = 1) {
    JoinRequest request{};
    for (size_t index = 0; index < kDeviceUidSize; ++index) {
        request.deviceUid[index] = static_cast<uint8_t>(uidSeed + index);
    }
    request.profileId = profileId;
    request.firmware = FirmwareVersion{1, 2, 3};
    request.requestNonce = nonce;
    return request;
}

class MemorySlotStorage final : public RegistrySlotStorage {
public:
    MemorySlotStorage() : data_{}, sizes_{0, 0}, present_{false, false} {}

    bool read(
        const uint8_t slot,
        uint8_t* const output,
        const size_t capacity,
        size_t& size) override {
        size = 0;
        if (slot >= 2 || !present_[slot] || sizes_[slot] > capacity) {
            return false;
        }
        for (size_t index = 0; index < sizes_[slot]; ++index) {
            output[index] = data_[slot][index];
        }
        size = sizes_[slot];
        return true;
    }

    bool write(
        const uint8_t slot,
        const uint8_t* const data,
        const size_t size) override {
        if (slot >= 2 || data == nullptr || size > kMaxRegistrySnapshotSize) {
            return false;
        }
        for (size_t index = 0; index < size; ++index) {
            data_[slot][index] = data[index];
        }
        sizes_[slot] = size;
        present_[slot] = true;
        return true;
    }

    void corrupt(const uint8_t slot, const size_t offset) {
        if (slot < 2 && present_[slot] && offset < sizes_[slot]) {
            data_[slot][offset] ^= 0x80;
        }
    }

private:
    uint8_t data_[2][kMaxRegistrySnapshotSize];
    size_t sizes_[2];
    bool present_[2];
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_reserves_stable_unique_ids() {
    NodeRegistry registry;
    const JoinRequest first = makeRequest(0x10, 0x1001, 0x11111111);
    const JoinRequest second = makeRequest(0x30, 0x1002, 0x22222222);

    const ReserveResult firstResult = registry.reserve(first);
    const ReserveResult secondResult = registry.reserve(second);
    TEST_ASSERT_EQUAL(static_cast<int>(ReserveStatus::Created),
                      static_cast<int>(firstResult.status));
    TEST_ASSERT_EQUAL_UINT8(1, firstResult.nodeId);
    TEST_ASSERT_EQUAL_UINT8(2, secondResult.nodeId);
    TEST_ASSERT_EQUAL_UINT32(2, registry.size());

    JoinRequest retry = first;
    retry.requestNonce = 0x33333333;
    const ReserveResult retryResult = registry.reserve(retry);
    TEST_ASSERT_EQUAL(static_cast<int>(ReserveStatus::ExistingPendingUpdated),
                      static_cast<int>(retryResult.status));
    TEST_ASSERT_EQUAL_UINT8(firstResult.nodeId, retryResult.nodeId);
    TEST_ASSERT_EQUAL_HEX32(
        retry.requestNonce,
        registry.findByUid(first.deviceUid)->requestNonce);
}

void test_profile_conflict_does_not_change_record() {
    NodeRegistry registry;
    const JoinRequest first = makeRequest(0x10, 0x1001);
    registry.reserve(first);
    JoinRequest conflicting = first;
    conflicting.profileId = 0x1002;

    const ReserveResult result = registry.reserve(conflicting);
    TEST_ASSERT_EQUAL(static_cast<int>(ReserveStatus::ProfileConflict),
                      static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_HEX16(
        first.profileId,
        registry.findByUid(first.deviceUid)->profileId);
}

void test_confirm_requires_uid_node_id_and_latest_nonce() {
    NodeRegistry registry;
    const JoinRequest request = makeRequest(0x10, 0x1001, 0x12345678);
    const ReserveResult reserved = registry.reserve(request);
    const JoinRequest other = makeRequest(0x20, 0x1001, request.requestNonce);

    TEST_ASSERT_EQUAL(
        static_cast<int>(ConfirmStatus::IdentityMismatch),
        static_cast<int>(registry.confirm(
            other.deviceUid, reserved.nodeId, request.requestNonce)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(ConfirmStatus::NonceMismatch),
        static_cast<int>(registry.confirm(
            request.deviceUid, reserved.nodeId, request.requestNonce + 1)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(ConfirmStatus::Confirmed),
        static_cast<int>(registry.confirm(
            request.deviceUid, reserved.nodeId, request.requestNonce)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(NodeState::Active),
        static_cast<int>(registry.findByNodeId(reserved.nodeId)->state));
    TEST_ASSERT_EQUAL(
        static_cast<int>(ConfirmStatus::AlreadyActive),
        static_cast<int>(registry.confirm(
            request.deviceUid, reserved.nodeId, request.requestNonce)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(ConfirmStatus::NonceMismatch),
        static_cast<int>(registry.confirm(
            request.deviceUid, reserved.nodeId, request.requestNonce + 1)));
}

void test_registry_capacity_is_bounded() {
    NodeRegistry registry;
    for (size_t index = 0; index < kMaxNodes; ++index) {
        const JoinRequest request = makeRequest(static_cast<uint8_t>(index * 3), 1);
        TEST_ASSERT_EQUAL(
            static_cast<int>(ReserveStatus::Created),
            static_cast<int>(registry.reserve(request).status));
    }

    const JoinRequest overflow = makeRequest(0xF0, 1);
    TEST_ASSERT_EQUAL(
        static_cast<int>(ReserveStatus::Full),
        static_cast<int>(registry.reserve(overflow).status));
    TEST_ASSERT_EQUAL_UINT32(kMaxNodes, registry.size());
}

void test_snapshot_round_trip_preserves_records() {
    NodeRegistry source;
    const JoinRequest request = makeRequest(0x10, 0x1234, 0x89ABCDEF);
    source.reserve(request);
    const char name[] = "\xD0\x94\xD0\xB0\xD1\x82\xD1\x87\xD0\xB8\xD0\xBA";
    TEST_ASSERT_EQUAL(
        static_cast<int>(RenameStatus::Renamed),
        static_cast<int>(source.rename(1, name, sizeof(name) - 1)));
    uint8_t encoded[kMaxRegistrySnapshotSize]{};
    size_t encodedSize = 0;

    TEST_ASSERT_EQUAL(
        static_cast<int>(SnapshotStatus::Ok),
        static_cast<int>(encodeRegistrySnapshot(
            source, 42, encoded, sizeof(encoded), encodedSize)));

    NodeRegistry decoded;
    uint32_t generation = 0;
    TEST_ASSERT_EQUAL(
        static_cast<int>(SnapshotStatus::Ok),
        static_cast<int>(decodeRegistrySnapshot(
            encoded, encodedSize, decoded, generation)));
    TEST_ASSERT_EQUAL_UINT32(42, generation);
    TEST_ASSERT_EQUAL_UINT32(1, decoded.size());
    const NodeRecord* record = decoded.findByUid(request.deviceUid);
    TEST_ASSERT_NOT_NULL(record);
    TEST_ASSERT_EQUAL_HEX16(request.profileId, record->profileId);
    TEST_ASSERT_EQUAL_HEX32(request.requestNonce, record->requestNonce);
    TEST_ASSERT_EQUAL_UINT8(sizeof(name) - 1, record->displayNameLength);
    TEST_ASSERT_EQUAL_MEMORY(name, record->displayName, sizeof(name) - 1);
}

void test_rename_accepts_utf8_and_rejects_invalid_names() {
    NodeRegistry registry;
    registry.reserve(makeRequest(0x10));
    const char ukrainian[] = "\xD0\x9A\xD1\x96\xD0\xBC\xD0\xBD\xD0\xB0\xD1\x82\xD0\xB0";
    TEST_ASSERT_EQUAL(
        static_cast<int>(RenameStatus::Renamed),
        static_cast<int>(registry.rename(1, ukrainian, sizeof(ukrainian) - 1)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(RenameStatus::NoChange),
        static_cast<int>(registry.rename(1, ukrainian, sizeof(ukrainian) - 1)));
    const char invalid[] = "\xC0\xAF";
    TEST_ASSERT_EQUAL(
        static_cast<int>(RenameStatus::InvalidName),
        static_cast<int>(registry.rename(1, invalid, sizeof(invalid) - 1)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(RenameStatus::NotFound),
        static_cast<int>(registry.rename(2, "other", 5)));
}

void test_snapshot_rejects_crc_corruption() {
    NodeRegistry source;
    source.reserve(makeRequest(0x10));
    uint8_t encoded[kMaxRegistrySnapshotSize]{};
    size_t encodedSize = 0;
    encodeRegistrySnapshot(source, 1, encoded, sizeof(encoded), encodedSize);
    encoded[kRegistryHeaderSize] ^= 0x01;

    NodeRegistry decoded;
    uint32_t generation = 0;
    TEST_ASSERT_EQUAL(
        static_cast<int>(SnapshotStatus::CrcMismatch),
        static_cast<int>(decodeRegistrySnapshot(
            encoded, encodedSize, decoded, generation)));
}

void test_dual_slot_falls_back_to_previous_valid_generation() {
    MemorySlotStorage slots;
    DualSlotRegistryStore writer(slots);
    NodeRegistry registry;
    registry.reserve(makeRequest(0x10));
    TEST_ASSERT_TRUE(writer.save(registry));  // generation 1, slot A
    registry.reserve(makeRequest(0x30));
    TEST_ASSERT_TRUE(writer.save(registry));  // generation 2, slot B
    slots.corrupt(1, kRegistryHeaderSize);

    DualSlotRegistryStore reader(slots);
    NodeRegistry recovered;
    TEST_ASSERT_EQUAL(
        static_cast<int>(LoadStatus::Loaded),
        static_cast<int>(reader.load(recovered)));
    TEST_ASSERT_EQUAL_UINT32(1, reader.generation());
    TEST_ASSERT_EQUAL_UINT32(1, recovered.size());
}

void test_dual_slot_loads_newest_valid_generation() {
    MemorySlotStorage slots;
    DualSlotRegistryStore writer(slots);
    NodeRegistry registry;
    registry.reserve(makeRequest(0x10));
    TEST_ASSERT_TRUE(writer.save(registry));
    registry.reserve(makeRequest(0x30));
    TEST_ASSERT_TRUE(writer.save(registry));

    DualSlotRegistryStore reader(slots);
    NodeRegistry recovered;
    TEST_ASSERT_EQUAL(
        static_cast<int>(LoadStatus::Loaded),
        static_cast<int>(reader.load(recovered)));
    TEST_ASSERT_EQUAL_UINT32(2, reader.generation());
    TEST_ASSERT_EQUAL_UINT32(2, recovered.size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reserves_stable_unique_ids);
    RUN_TEST(test_profile_conflict_does_not_change_record);
    RUN_TEST(test_confirm_requires_uid_node_id_and_latest_nonce);
    RUN_TEST(test_registry_capacity_is_bounded);
    RUN_TEST(test_snapshot_round_trip_preserves_records);
    RUN_TEST(test_rename_accepts_utf8_and_rejects_invalid_names);
    RUN_TEST(test_snapshot_rejects_crc_corruption);
    RUN_TEST(test_dual_slot_falls_back_to_previous_valid_generation);
    RUN_TEST(test_dual_slot_loads_newest_valid_generation);
    return UNITY_END();
}
