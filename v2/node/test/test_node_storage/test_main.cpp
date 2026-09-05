#include <NodeStorage.h>
#include <TelemetrySchedule.h>
#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace radiosensors::node::storage;
using radiosensors::node::CounterReportSchedule;
using radiosensors::node::DoorReportSchedule;
using radiosensors::node::RollingKeepAlive;
using radiosensors::node::RadioRetryBackoff;
using radiosensors::node::kCounterMinimumReportMs;

namespace {

class FakeStorage {
public:
    FakeStorage() { memset(bytes, 0xFF, sizeof(bytes)); }

    uint8_t read(const size_t address) const { return bytes[address]; }
    void update(const size_t address, const uint8_t value) {
        bytes[address] = value;
    }

    uint8_t bytes[kEepromSize];
};

NetworkConfig makeConfig(const uint8_t nodeId = 7) {
    NetworkConfig value{};
    value.state = ProvisioningState::Active;
    value.powerLevel = 12;
    value.nodeId = nodeId;
    value.gatewayId = 100;
    value.networkId = 42;
    for (uint8_t index = 0; index < sizeof(value.installationKey); ++index) {
        value.installationKey[index] = static_cast<uint8_t>(index + 1);
    }
    value.requestNonce = 0x89ABCDEFUL;
    value.lastPowerCommandId = 0x1234;
    return value;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_layout_fills_eeprom_without_overlap() {
    TEST_ASSERT_EQUAL_UINT32(32, kNetworkConfigSlotSize);
    TEST_ASSERT_EQUAL_UINT32(0x20, kNetworkConfigSlotB);
    TEST_ASSERT_EQUAL_UINT32(0x40, kSetCountSlotA);
    TEST_ASSERT_EQUAL_UINT32(0x60, kCounterRingStart);
    TEST_ASSERT_EQUAL_UINT32(
        kEepromSize,
        kCounterRingStart + kCounterRecordSize * kCounterRecordCount);
}

void test_network_config_round_trip_and_newest_slot() {
    FakeStorage memory;
    NetworkConfigStore<FakeStorage> writer(memory);
    NetworkConfig first = makeConfig();
    TEST_ASSERT_TRUE(writer.save(first));
    NetworkConfig second = first;
    second.powerLevel = 19;
    TEST_ASSERT_TRUE(writer.save(second));

    NetworkConfigStore<FakeStorage> reader(memory);
    NetworkConfig restored{};
    TEST_ASSERT_TRUE(reader.load(restored));
    TEST_ASSERT_EQUAL_UINT8(19, restored.powerLevel);
    TEST_ASSERT_EQUAL_UINT8(1, restored.generation);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFUL, restored.requestNonce);
    TEST_ASSERT_EQUAL_HEX16(0x1234, restored.lastPowerCommandId);
}

void test_network_config_falls_back_from_corrupt_new_slot() {
    FakeStorage memory;
    NetworkConfigStore<FakeStorage> writer(memory);
    NetworkConfig first = makeConfig(8);
    TEST_ASSERT_TRUE(writer.save(first));
    NetworkConfig second = first;
    second.nodeId = 9;
    TEST_ASSERT_TRUE(writer.save(second));
    memory.bytes[kNetworkConfigSlotB + 12] ^= 0x40;

    NetworkConfigStore<FakeStorage> reader(memory);
    NetworkConfig restored{};
    TEST_ASSERT_TRUE(reader.load(restored));
    TEST_ASSERT_EQUAL_UINT8(8, restored.nodeId);
}

void test_network_config_generation_wrap_selects_latest() {
    FakeStorage storage;
    NetworkConfigStore<FakeStorage> store(storage);
    NetworkConfig value = makeConfig();

    for (uint16_t generation = 0; generation < 300; ++generation) {
        value.powerLevel = static_cast<uint8_t>(generation % 32U);
        TEST_ASSERT_TRUE(store.save(value));
    }

    NetworkConfig restored{};
    TEST_ASSERT_TRUE(store.load(restored));
    TEST_ASSERT_EQUAL_UINT8(299U % 32U, restored.powerLevel);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(299U), restored.generation);
}

void test_factory_reset_preserves_counter_area() {
    FakeStorage memory;
    CounterStore<FakeStorage> counter(memory);
    TEST_ASSERT_TRUE(counter.save(12345));
    NetworkConfigStore<FakeStorage> config(memory);
    NetworkConfig value = makeConfig();
    TEST_ASSERT_TRUE(config.save(value));
    config.factoryReset();

    NetworkConfigStore<FakeStorage> configAfterReset(memory);
    TEST_ASSERT_FALSE(configAfterReset.load(value));
    CounterStore<FakeStorage> counterAfterReset(memory);
    uint32_t restored = 0;
    TEST_ASSERT_TRUE(counterAfterReset.load(restored));
    TEST_ASSERT_EQUAL_UINT32(12345, restored);
}

void test_counter_ring_restores_latest_across_wraps() {
    FakeStorage memory;
    CounterStore<FakeStorage> writer(memory);
    for (uint32_t value = 1; value <= 600; ++value) {
        TEST_ASSERT_TRUE(writer.save(value));
    }
    CounterStore<FakeStorage> reader(memory);
    uint32_t restored = 0;
    TEST_ASSERT_TRUE(reader.load(restored));
    TEST_ASSERT_EQUAL_UINT32(600, restored);
}

void test_counter_ignores_uncommitted_record() {
    FakeStorage memory;
    CounterStore<FakeStorage> writer(memory);
    TEST_ASSERT_TRUE(writer.save(41));
    const size_t interrupted = kCounterRingStart + kCounterRecordSize;
    memory.bytes[interrupted] = 0xFF;
    uint8_t bytes[4];
    write32(bytes, 42);
    memcpy(memory.bytes + interrupted + 1, bytes, sizeof(bytes));

    CounterStore<FakeStorage> reader(memory);
    uint32_t restored = 0;
    TEST_ASSERT_TRUE(reader.load(restored));
    TEST_ASSERT_EQUAL_UINT32(41, restored);
}

void test_set_count_result_round_trip_and_corruption_fallback() {
    FakeStorage memory;
    SetCountStore<FakeStorage> writer(memory);
    SetCountResult pending{};
    pending.status = SetCountStatus::Pending;
    pending.commandId = 17;
    pending.oldCount = 100;
    pending.appliedCount = 500;
    TEST_ASSERT_TRUE(writer.save(pending));
    SetCountResult applied = pending;
    applied.status = SetCountStatus::Applied;
    TEST_ASSERT_TRUE(writer.save(applied));

    SetCountStore<FakeStorage> reader(memory);
    SetCountResult restored{};
    TEST_ASSERT_TRUE(reader.load(restored));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountStatus::Applied),
        static_cast<uint8_t>(restored.status));
    TEST_ASSERT_EQUAL_UINT16(17, restored.commandId);
    TEST_ASSERT_EQUAL_UINT32(100, restored.oldCount);
    TEST_ASSERT_EQUAL_UINT32(500, restored.appliedCount);

    memory.bytes[kSetCountSlotB + 9] ^= 0x01;
    SetCountStore<FakeStorage> fallbackReader(memory);
    TEST_ASSERT_TRUE(fallbackReader.load(restored));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountStatus::Pending),
        static_cast<uint8_t>(restored.status));
}

void test_set_count_result_generation_wrap_selects_latest() {
    FakeStorage storage;
    SetCountStore<FakeStorage> store(storage);
    SetCountResult value{};

    for (uint16_t commandId = 1; commandId <= 300; ++commandId) {
        value.status = SetCountStatus::Applied;
        value.commandId = commandId;
        value.oldCount = commandId - 1U;
        value.appliedCount = commandId;
        TEST_ASSERT_TRUE(store.save(value));
    }

    SetCountResult restored{};
    TEST_ASSERT_TRUE(store.load(restored));
    TEST_ASSERT_EQUAL_UINT16(300U, restored.commandId);
    TEST_ASSERT_EQUAL_UINT32(300U, restored.appliedCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(299U), restored.generation);
}

void test_event_keep_alive_rolls_from_successful_event() {
    DoorReportSchedule schedule;
    TEST_ASSERT_TRUE(schedule.due(0));
    schedule.transmissionSucceeded(0);
    TEST_ASSERT_FALSE(schedule.due(55UL * 60UL * 1000UL));
    schedule.stateChanged();
    TEST_ASSERT_TRUE(schedule.due(55UL * 60UL * 1000UL));
    schedule.transmissionSucceeded(55UL * 60UL * 1000UL);
    TEST_ASSERT_FALSE(schedule.due(60UL * 60UL * 1000UL));
    TEST_ASSERT_TRUE(schedule.due(115UL * 60UL * 1000UL));
}

void test_failed_door_transmission_keeps_event_pending() {
    DoorReportSchedule schedule;
    schedule.transmissionSucceeded(0);
    schedule.stateChanged();
    TEST_ASSERT_TRUE(schedule.due(1000));
    TEST_ASSERT_TRUE(schedule.due(2000));
}

void test_counter_coalesces_pulses_for_one_minute() {
    CounterReportSchedule schedule;
    schedule.transmissionSucceeded(0);
    schedule.pulseRecorded();
    TEST_ASSERT_FALSE(schedule.due(kCounterMinimumReportMs - 1U));
    schedule.pulseRecorded();
    TEST_ASSERT_TRUE(schedule.due(kCounterMinimumReportMs));
    schedule.transmissionSucceeded(kCounterMinimumReportMs);
    TEST_ASSERT_FALSE(schedule.due(kCounterMinimumReportMs + 1U));
}

void test_keep_alive_handles_clock_wrap() {
    RollingKeepAlive schedule(1000);
    schedule.transmissionSucceeded(0xFFFFFF00UL);
    TEST_ASSERT_FALSE(schedule.due(0x00000100UL));
    TEST_ASSERT_TRUE(schedule.due(0x00000300UL));
}

void test_radio_retry_uses_bounded_exponential_backoff() {
    RadioRetryBackoff retry;
    TEST_ASSERT_TRUE(retry.allowed(0));
    retry.failed(0);
    TEST_ASSERT_FALSE(retry.allowed(59999));
    TEST_ASSERT_TRUE(retry.allowed(60000));
    retry.failed(60000);
    TEST_ASSERT_FALSE(retry.allowed(359999));
    TEST_ASSERT_TRUE(retry.allowed(360000));
    retry.failed(360000);
    TEST_ASSERT_TRUE(retry.allowed(1260000));
    retry.failed(1260000);
    TEST_ASSERT_FALSE(retry.allowed(4859999));
    TEST_ASSERT_TRUE(retry.allowed(4860000));
    retry.failed(4860000);
    TEST_ASSERT_TRUE(retry.allowed(8460000));
    retry.succeeded();
    TEST_ASSERT_TRUE(retry.allowed(8460001));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_fills_eeprom_without_overlap);
    RUN_TEST(test_network_config_round_trip_and_newest_slot);
    RUN_TEST(test_network_config_falls_back_from_corrupt_new_slot);
    RUN_TEST(test_network_config_generation_wrap_selects_latest);
    RUN_TEST(test_factory_reset_preserves_counter_area);
    RUN_TEST(test_counter_ring_restores_latest_across_wraps);
    RUN_TEST(test_counter_ignores_uncommitted_record);
    RUN_TEST(test_set_count_result_round_trip_and_corruption_fallback);
    RUN_TEST(test_set_count_result_generation_wrap_selects_latest);
    RUN_TEST(test_event_keep_alive_rolls_from_successful_event);
    RUN_TEST(test_failed_door_transmission_keeps_event_pending);
    RUN_TEST(test_counter_coalesces_pulses_for_one_minute);
    RUN_TEST(test_keep_alive_handles_clock_wrap);
    RUN_TEST(test_radio_retry_uses_bounded_exponential_backoff);
    return UNITY_END();
}
