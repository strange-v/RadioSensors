#include <ConfirmedInput.h>
#include <CounterStorage.h>
#include <RadioPowerState.h>
#include <SupplyVoltage.h>
#include <TelemetrySchedule.h>
#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace radiosensors::node::storage;
using radiosensors::node::CounterReportSchedule;
using radiosensors::node::ClimateReportPolicy;
using radiosensors::node::ConfirmedInput;
using radiosensors::node::InputChange;
using radiosensors::node::MinimumPhaseFilter;
using radiosensors::node::BinaryReportSchedule;
using radiosensors::node::LoadedSupplyVoltage;
using radiosensors::node::RollingKeepAlive;
using radiosensors::node::RadioRetryBackoff;
using radiosensors::node::HintedSessionPolicy;
using radiosensors::node::PowerDecision;
using radiosensors::node::afterAcknowledged;
using radiosensors::node::afterUnacknowledged;
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
    value.nodeId = nodeId;
    value.gatewayId = 100;
    value.networkId = 42;
    for (uint8_t index = 0; index < sizeof(value.installationKey); ++index) {
        value.installationKey[index] = static_cast<uint8_t>(index + 1);
    }
    value.requestNonce = 0x89ABCDEFUL;
    return value;
}

class FakeContact {
public:
    bool readOnce() {
        ++reads;
        return level;
    }

    bool sample(bool& high) {
        ++bursts;
        if (!settles) return false;
        high = settledLevel;
        return true;
    }

    void set(const bool high) {
        level = high;
        settledLevel = high;
    }

    bool level = true;
    bool settledLevel = true;
    bool settles = true;
    int reads = 0;
    int bursts = 0;
};

FactoryCredentials makeFactoryCredentials() {
    FactoryCredentials value{};
    for (uint8_t index = 0; index < sizeof(value.key); ++index)
        value.key[index] = static_cast<uint8_t>(0x10 + index);
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

void test_factory_credentials_round_trip_and_validation() {
    FactoryCredentials source{};
    for (uint8_t index = 0; index < sizeof(source.key); ++index)
        source.key[index] = static_cast<uint8_t>(index + 1);
    uint8_t bytes[kFactoryCredentialSize];
    TEST_ASSERT_TRUE(encodeFactoryCredentials(source, bytes, sizeof(bytes)));
    const uint8_t expected[kFactoryCredentialSize] = {
        0x52, 0x53, 0x46, 0x43, 0x01, 0x01, 0x00, 0x00,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
        0xAE, 0xDC, 0x51, 0x23, 0x00, 0x00, 0x00, 0x00,
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, bytes, sizeof(expected));
    FactoryCredentials restored{};
    TEST_ASSERT_TRUE(decodeFactoryCredentials(bytes, sizeof(bytes), restored));
    TEST_ASSERT_EQUAL_MEMORY(source.key, restored.key, sizeof(source.key));

    bytes[12] ^= 0x01;
    TEST_ASSERT_FALSE(decodeFactoryCredentials(bytes, sizeof(bytes), restored));
    memset(bytes, 0xFF, sizeof(bytes));
    TEST_ASSERT_FALSE(decodeFactoryCredentials(bytes, sizeof(bytes), restored));
}

void test_factory_credential_store_reads_user_row_independently() {
    FakeStorage userRow;
    const FactoryCredentials source = makeFactoryCredentials();
    uint8_t bytes[kFactoryCredentialSize];
    TEST_ASSERT_TRUE(encodeFactoryCredentials(source, bytes, sizeof(bytes)));
    memcpy(userRow.bytes, bytes, sizeof(bytes));
    FactoryCredentialStore<FakeStorage> store(userRow);
    FactoryCredentials restored{};
    TEST_ASSERT_TRUE(store.load(restored));
    TEST_ASSERT_EQUAL_MEMORY(source.key, restored.key, sizeof(source.key));

    FakeStorage eeprom;
    NetworkConfigStore<FakeStorage> network(eeprom);
    NetworkConfig config = makeConfig();
    TEST_ASSERT_TRUE(network.save(config));
    network.factoryReset();
    TEST_ASSERT_TRUE(store.load(restored));
}

void test_network_config_round_trip_and_newest_slot() {
    FakeStorage memory;
    NetworkConfigStore<FakeStorage> writer(memory);
    NetworkConfig first = makeConfig();
    TEST_ASSERT_TRUE(writer.save(first));
    NetworkConfig second = first;
    second.networkId = 43;
    TEST_ASSERT_TRUE(writer.save(second));

    NetworkConfigStore<FakeStorage> reader(memory);
    NetworkConfig restored{};
    TEST_ASSERT_TRUE(reader.load(restored));
    TEST_ASSERT_EQUAL_UINT8(43, restored.networkId);
    TEST_ASSERT_EQUAL_UINT8(1, restored.generation);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFUL, restored.requestNonce);
}

void test_network_config_rejects_nonzero_reserved_bytes() {
    uint8_t bytes[kNetworkConfigSlotSize];
    NetworkConfig value = makeConfig();
    TEST_ASSERT_TRUE(encodeNetworkConfig(value, bytes, sizeof(bytes)));
    bytes[28] = 0x01;
    write16(bytes + 30, crc16Ccitt(bytes, 30));
    NetworkConfig restored{};
    TEST_ASSERT_FALSE(decodeNetworkConfig(bytes, sizeof(bytes), restored));
}

void test_power_target_is_clamped_and_ends_a_fallback() {
    PowerDecision decision = afterAcknowledged(2, false, false, 0, 5);
    TEST_ASSERT_FALSE(decision.change);
    decision = afterAcknowledged(2, false, true, 2, 5);
    TEST_ASSERT_FALSE(decision.change);
    decision = afterAcknowledged(2, false, true, 9, 5);
    TEST_ASSERT_TRUE(decision.change);
    TEST_ASSERT_EQUAL_UINT8(5, decision.level);
    TEST_ASSERT_FALSE(decision.fallback);
    decision = afterAcknowledged(5, true, true, 3, 5);
    TEST_ASSERT_TRUE(decision.change);
    TEST_ASSERT_EQUAL_UINT8(3, decision.level);
    TEST_ASSERT_FALSE(decision.fallback);
}

void test_node_falls_back_to_its_ceiling_after_three_lost_reports() {
    TEST_ASSERT_FALSE(afterUnacknowledged(1, false, 2, 5).change);
    const PowerDecision decision = afterUnacknowledged(1, false, 3, 5);
    TEST_ASSERT_TRUE(decision.change);
    TEST_ASSERT_EQUAL_UINT8(5, decision.level);
    TEST_ASSERT_TRUE(decision.fallback);
    TEST_ASSERT_FALSE(afterUnacknowledged(5, false, 3, 5).change);
    TEST_ASSERT_FALSE(afterUnacknowledged(5, true, 9, 5).change);
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
        value.networkId = static_cast<uint8_t>(generation);
        TEST_ASSERT_TRUE(store.save(value));
    }

    NetworkConfig restored{};
    TEST_ASSERT_TRUE(store.load(restored));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(299U), restored.networkId);
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

void test_set_count_journal_applies_and_recognizes_redelivery() {
    FakeStorage memory;
    CounterStore<FakeStorage> counter(memory);
    SetCountStore<FakeStorage> results(memory);
    SetCountJournal<FakeStorage> journal(counter, results);
    uint32_t count = 100;
    TEST_ASSERT_TRUE(counter.save(count));
    journal.recover(count);

    SetCountResult result{};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountOutcome::Applied),
        static_cast<uint8_t>(journal.apply(17, 500, count, result)));
    TEST_ASSERT_EQUAL_UINT32(500, count);
    TEST_ASSERT_EQUAL_UINT32(100, result.oldCount);
    TEST_ASSERT_EQUAL_UINT32(500, result.appliedCount);

    // Pulses counted after the command must survive its redelivery.
    count = 503;
    TEST_ASSERT_TRUE(counter.save(count));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountOutcome::Redelivered),
        static_cast<uint8_t>(journal.apply(17, 500, count, result)));
    TEST_ASSERT_EQUAL_UINT32(503, count);
    TEST_ASSERT_EQUAL_UINT32(100, result.oldCount);
    TEST_ASSERT_EQUAL_UINT32(500, result.appliedCount);

    SetCountStore<FakeStorage> reader(memory);
    SetCountResult stored{};
    TEST_ASSERT_TRUE(reader.load(stored));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountStatus::Applied),
        static_cast<uint8_t>(stored.status));
    TEST_ASSERT_EQUAL_UINT16(17, stored.commandId);
}

void test_set_count_journal_completes_an_interrupted_apply_on_boot() {
    FakeStorage memory;
    {
        CounterStore<FakeStorage> counter(memory);
        SetCountStore<FakeStorage> results(memory);
        TEST_ASSERT_TRUE(counter.save(100));
        // Reset after the Pending result, before the ring write.
        SetCountResult pending{};
        pending.status = SetCountStatus::Pending;
        pending.commandId = 9;
        pending.oldCount = 100;
        pending.appliedCount = 42;
        TEST_ASSERT_TRUE(results.save(pending));
    }

    CounterStore<FakeStorage> counter(memory);
    SetCountStore<FakeStorage> results(memory);
    SetCountJournal<FakeStorage> journal(counter, results);
    uint32_t count = 0;
    TEST_ASSERT_TRUE(counter.load(count));
    TEST_ASSERT_EQUAL_UINT32(100, count);
    journal.recover(count);
    TEST_ASSERT_EQUAL_UINT32(42, count);

    CounterStore<FakeStorage> ringReader(memory);
    uint32_t persisted = 0;
    TEST_ASSERT_TRUE(ringReader.load(persisted));
    TEST_ASSERT_EQUAL_UINT32(42, persisted);
    SetCountResult result{};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountOutcome::Redelivered),
        static_cast<uint8_t>(journal.apply(9, 42, count, result)));
}

void test_set_count_journal_forgets_ids_after_commissioning() {
    FakeStorage memory;
    CounterStore<FakeStorage> counter(memory);
    SetCountStore<FakeStorage> results(memory);
    SetCountJournal<FakeStorage> journal(counter, results);
    uint32_t count = 0;
    journal.recover(count);
    SetCountResult result{};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountOutcome::Applied),
        static_cast<uint8_t>(journal.apply(5, 10, count, result)));

    journal.forgetCommandIds(count);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SetCountOutcome::Applied),
        static_cast<uint8_t>(journal.apply(5, 20, count, result)));
    TEST_ASSERT_EQUAL_UINT32(20, count);
    TEST_ASSERT_EQUAL_UINT32(10, result.oldCount);
}

void test_hinted_sessions_are_rate_limited() {
    HintedSessionPolicy policy;
    const uint32_t minute = 60UL * 1000UL;
    TEST_ASSERT_TRUE(policy.allowed(0));
    policy.finished(0, true);
    TEST_ASSERT_FALSE(policy.allowed(5U * minute - 1U));
    TEST_ASSERT_TRUE(policy.allowed(5U * minute));

    // Unanswered sessions add the radio retry delays on top.
    policy.finished(5U * minute, false);
    TEST_ASSERT_TRUE(policy.allowed(10U * minute));
    policy.finished(10U * minute, false);
    TEST_ASSERT_TRUE(policy.allowed(15U * minute));
    policy.finished(15U * minute, false);
    TEST_ASSERT_FALSE(policy.allowed(30U * minute - 1U));
    TEST_ASSERT_TRUE(policy.allowed(30U * minute));

    policy.finished(30U * minute, true);
    TEST_ASSERT_TRUE(policy.allowed(35U * minute));
}

void test_event_keep_alive_rolls_from_successful_event() {
    BinaryReportSchedule schedule;
    TEST_ASSERT_TRUE(schedule.due(0));
    schedule.transmissionSucceeded(0);
    TEST_ASSERT_FALSE(schedule.due(55UL * 60UL * 1000UL));
    schedule.stateChanged();
    TEST_ASSERT_TRUE(schedule.due(55UL * 60UL * 1000UL));
    schedule.transmissionSucceeded(55UL * 60UL * 1000UL);
    TEST_ASSERT_FALSE(schedule.due(60UL * 60UL * 1000UL));
    TEST_ASSERT_TRUE(schedule.due(115UL * 60UL * 1000UL));
}

void test_failed_binary_transmission_keeps_event_pending() {
    BinaryReportSchedule schedule;
    schedule.transmissionSucceeded(0);
    schedule.stateChanged();
    TEST_ASSERT_TRUE(schedule.due(1000));
    TEST_ASSERT_TRUE(schedule.due(2000));
}

void test_binary_state_change_is_urgent_once() {
    BinaryReportSchedule schedule;
    TEST_ASSERT_FALSE(schedule.takeUrgent());
    schedule.stateChanged();
    TEST_ASSERT_TRUE(schedule.takeUrgent());
    TEST_ASSERT_FALSE(schedule.takeUrgent());
    TEST_ASSERT_TRUE(schedule.due(0));
    schedule.stateChanged();
    TEST_ASSERT_TRUE(schedule.takeUrgent());
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

void test_setting_counter_is_urgent_once() {
    CounterReportSchedule schedule;
    schedule.transmissionSucceeded(0);
    schedule.countSet();
    TEST_ASSERT_TRUE(schedule.due(1));
    TEST_ASSERT_TRUE(schedule.takeUrgent());
    TEST_ASSERT_FALSE(schedule.takeUrgent());
    TEST_ASSERT_TRUE(schedule.due(1));
    schedule.transmissionSucceeded(1);
    TEST_ASSERT_FALSE(schedule.due(2));
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

void test_fixed_climate_policy_ignores_supply_voltage() {
    const ClimateReportPolicy policy = ClimateReportPolicy::fixed(900000UL);
    TEST_ASSERT_EQUAL_UINT32(900000UL, policy.intervalForMillivolts(1800));
    TEST_ASSERT_EQUAL_UINT32(900000UL, policy.intervalForMillivolts(3300));
}

void test_adaptive_climate_policy_uses_v1_threshold_semantics() {
    const ClimateReportPolicy policy = ClimateReportPolicy::adaptive(
        60000UL, 300000UL, 2500);
    TEST_ASSERT_EQUAL_UINT32(300000UL, policy.intervalForMillivolts(2499));
    TEST_ASSERT_EQUAL_UINT32(300000UL, policy.intervalForMillivolts(2500));
    TEST_ASSERT_EQUAL_UINT32(60000UL, policy.intervalForMillivolts(2501));
}

void test_supply_voltage_reports_lower_of_before_and_previous_after() {
    LoadedSupplyVoltage voltage;
    TEST_ASSERT_EQUAL_UINT16(3000, voltage.report(3000));
    voltage.transmitted(2800);
    TEST_ASSERT_EQUAL_UINT16(2800, voltage.report(2950));
    TEST_ASSERT_EQUAL_UINT16(2700, voltage.report(2700));
    voltage.transmitted(2990);
    TEST_ASSERT_EQUAL_UINT16(2960, voltage.report(2960));
}

void test_confirmed_input_bursts_only_on_disagreeing_read() {
    FakeContact contact;
    ConfirmedInput input(true);
    for (int tick = 0; tick < 10; ++tick) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(InputChange::None),
            static_cast<uint8_t>(input.update(contact)));
    }
    TEST_ASSERT_EQUAL_INT(10, contact.reads);
    TEST_ASSERT_EQUAL_INT(0, contact.bursts);

    contact.set(false);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::Fell),
        static_cast<uint8_t>(input.update(contact)));
    TEST_ASSERT_EQUAL_INT(1, contact.bursts);
    TEST_ASSERT_FALSE(input.high());
}

void test_confirmed_input_rejects_glitch_and_unsettled_burst() {
    FakeContact contact;
    ConfirmedInput input(true);
    contact.level = false;
    contact.settledLevel = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::None),
        static_cast<uint8_t>(input.update(contact)));
    TEST_ASSERT_TRUE(input.high());

    contact.settles = false;
    contact.settledLevel = false;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::None),
        static_cast<uint8_t>(input.update(contact)));
    TEST_ASSERT_TRUE(input.high());

    contact.settles = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::Fell),
        static_cast<uint8_t>(input.update(contact)));
}

void test_confirmed_input_counts_boot_inside_low_phase_once() {
    FakeContact contact;
    contact.set(false);
    ConfirmedInput input(true);
    int rises = 0;
    const bool levels[] = {false, false, true, true, false, true};
    for (const bool level : levels) {
        contact.set(level);
        if (input.update(contact) == InputChange::Rose) ++rises;
    }
    TEST_ASSERT_EQUAL_INT(2, rises);
}

void test_minimum_phase_rejects_chatter_and_counts_one_rise_per_pulse() {
    MinimumPhaseFilter phase(500, true);
    // Level sampled every 250 ms: chatter while the magnet approaches, a held
    // LOW, chatter while it leaves, then a held HIGH.
    const bool levels[] = {
        true, false, true, false, false, false, false,
        true, false, true, true, true, true,
    };
    int falls = 0;
    int rises = 0;
    uint32_t now = 0;
    for (const bool level : levels) {
        const InputChange change = phase.update(now, level);
        if (change == InputChange::Fell) ++falls;
        if (change == InputChange::Rose) ++rises;
        now += 250;
    }
    TEST_ASSERT_EQUAL_INT(1, falls);
    TEST_ASSERT_EQUAL_INT(1, rises);
}

void test_minimum_phase_accepts_level_held_for_minimum_time() {
    MinimumPhaseFilter phase(500, true);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::None),
        static_cast<uint8_t>(phase.update(1000, false)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::None),
        static_cast<uint8_t>(phase.update(1250, false)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::Fell),
        static_cast<uint8_t>(phase.update(1500, false)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(InputChange::None),
        static_cast<uint8_t>(phase.update(1750, false)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_fills_eeprom_without_overlap);
    RUN_TEST(test_factory_credentials_round_trip_and_validation);
    RUN_TEST(test_factory_credential_store_reads_user_row_independently);
    RUN_TEST(test_network_config_round_trip_and_newest_slot);
    RUN_TEST(test_network_config_rejects_nonzero_reserved_bytes);
    RUN_TEST(test_power_target_is_clamped_and_ends_a_fallback);
    RUN_TEST(test_node_falls_back_to_its_ceiling_after_three_lost_reports);
    RUN_TEST(test_network_config_falls_back_from_corrupt_new_slot);
    RUN_TEST(test_network_config_generation_wrap_selects_latest);
    RUN_TEST(test_factory_reset_preserves_counter_area);
    RUN_TEST(test_counter_ring_restores_latest_across_wraps);
    RUN_TEST(test_counter_ignores_uncommitted_record);
    RUN_TEST(test_set_count_result_round_trip_and_corruption_fallback);
    RUN_TEST(test_set_count_result_generation_wrap_selects_latest);
    RUN_TEST(test_set_count_journal_applies_and_recognizes_redelivery);
    RUN_TEST(test_set_count_journal_completes_an_interrupted_apply_on_boot);
    RUN_TEST(test_set_count_journal_forgets_ids_after_commissioning);
    RUN_TEST(test_hinted_sessions_are_rate_limited);
    RUN_TEST(test_event_keep_alive_rolls_from_successful_event);
    RUN_TEST(test_failed_binary_transmission_keeps_event_pending);
    RUN_TEST(test_binary_state_change_is_urgent_once);
    RUN_TEST(test_counter_coalesces_pulses_for_one_minute);
    RUN_TEST(test_setting_counter_is_urgent_once);
    RUN_TEST(test_keep_alive_handles_clock_wrap);
    RUN_TEST(test_radio_retry_uses_bounded_exponential_backoff);
    RUN_TEST(test_fixed_climate_policy_ignores_supply_voltage);
    RUN_TEST(test_adaptive_climate_policy_uses_v1_threshold_semantics);
    RUN_TEST(test_supply_voltage_reports_lower_of_before_and_previous_after);
    RUN_TEST(test_confirmed_input_bursts_only_on_disagreeing_read);
    RUN_TEST(test_confirmed_input_rejects_glitch_and_unsettled_burst);
    RUN_TEST(test_confirmed_input_counts_boot_inside_low_phase_once);
    RUN_TEST(test_minimum_phase_rejects_chatter_and_counts_one_rise_per_pulse);
    RUN_TEST(test_minimum_phase_accepts_level_held_for_minimum_time);
    return UNITY_END();
}
