#pragma once

#include <ProfileIds.h>
#include <TelemetryFrames.h>
#include <stddef.h>
#include <stdint.h>

#include "ArduinoEepromStorage.h"
#include "ConfirmedInput.h"
#include "CounterStorage.h"
#include "PolledReedInput.h"
#include "TelemetrySchedule.h"

namespace radiosensors {
namespace node {

class CounterReedProfile {
public:
    static constexpr uint16_t kProfileId =
        protocol::profileIdValue(protocol::ProfileId::PulseCounter);
    static constexpr size_t kTelemetrySize = protocol::kCounterTelemetrySize;

    // Starting from HIGH is correct for counting: a boot inside a LOW phase
    // first confirms a fall, so that pulse is counted once, on its rise.
    CounterReedProfile(const pin_size_t pin, const uint32_t minimumPhaseMs)
        : input_(pin),
          contact_(true),
          phase_(minimumPhaseMs, true),
          store_(eeprom_) {}

    void begin() {
        input_.sleep();
        store_.load(count_);
#if defined(NODE_DEBUG)
        Serial.print(F("counter: restored "));
        Serial.println(count_);
#endif
    }

    void poll(const uint32_t now) {
        const InputChange contactChange = contact_.update(input_);
#if defined(NODE_DEBUG)
        if (contactChange != InputChange::None) {
            Serial.print(F("counter: contact "));
            Serial.print(contactChange == InputChange::Rose
                ? F("open at ")
                : F("closed at "));
            Serial.println(now);
        }
#else
        (void)contactChange;
#endif
        if (phase_.update(now, contact_.high()) != InputChange::Rose) return;
        ++count_;
        store_.save(count_);
        reportSchedule_.pulseRecorded();
#if defined(NODE_DEBUG)
        Serial.print(F("counter: pulse "));
        Serial.println(count_);
#endif
    }

    bool reportDue(const uint32_t now) const {
        return reportSchedule_.due(now);
    }

    size_t encodeTelemetry(
        const uint16_t supplyMillivolts, uint8_t* output,
        const size_t capacity) {
        return protocol::encodeCounterTelemetry(
                   supplyMillivolts, count_, output, capacity) ==
                protocol::TelemetryCodecStatus::Ok
            ? kTelemetrySize
            : 0;
    }

    void reportAcknowledged(const uint32_t now, uint16_t) {
        reportSchedule_.transmissionSucceeded(now);
    }

private:
    PolledReedInput input_;
    ConfirmedInput contact_;
    MinimumPhaseFilter phase_;
    storage::ArduinoEepromStorage eeprom_;
    storage::CounterStore<storage::ArduinoEepromStorage> store_;
    CounterReportSchedule reportSchedule_;
    uint32_t count_ = 0;
};

}  // namespace node
}  // namespace radiosensors
