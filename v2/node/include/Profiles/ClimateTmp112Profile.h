#pragma once

#include <CommandSessionFrames.h>
#include <ProfileIds.h>
#include <TelemetryFrames.h>
#include <stddef.h>
#include <stdint.h>

#include "DebugLog.h"
#include "TelemetrySchedule.h"
#include "Tmp112Sensor.h"

namespace radiosensors {
namespace node {

class ClimateTmp112Profile {
public:
    static constexpr uint16_t kProfileId =
        protocol::profileIdValue(protocol::ProfileId::Temperature);
    static constexpr size_t kTelemetrySize =
        protocol::kTemperatureTelemetrySize;

    ClimateTmp112Profile(
        TwoWire& wire, const uint8_t address,
        const ClimateReportPolicy reportPolicy)
        : temperature_(wire, address),
          reportPolicy_(reportPolicy),
          reportSchedule_(reportPolicy.intervalForMillivolts(0)) {}

    void begin() { temperature_.begin(); }

    void poll(uint32_t) {}

    bool reportDue(const uint32_t now) const {
        return reportSchedule_.due(now);
    }

    bool takeUrgentReport() { return false; }

    size_t encodeTelemetry(
        const protocol::TelemetryPrefix& prefix, uint8_t* output,
        const size_t capacity) {
        int16_t temperature = protocol::kInvalidTemperature;
        temperature_.readTemperature(temperature);
        return protocol::encodeTemperatureTelemetry(
                   prefix, temperature, output, capacity) ==
                protocol::TelemetryCodecStatus::Ok
            ? kTelemetrySize
            : 0;
    }

    void reportAcknowledged(
        const uint32_t now, const uint16_t supplyMillivolts) {
        const uint32_t nextInterval =
            reportPolicy_.intervalForMillivolts(supplyMillivolts);
        reportSchedule_.setInterval(nextInterval);
        reportSchedule_.transmissionSucceeded(now);
#if defined(NODE_DEBUG)
        debugValue(F("clim next s="), nextInterval / 1000UL);
#endif
    }

    void applyCommand(const protocol::Command&, protocol::CommandResult&) {}
    void commissioned() {}

private:
    Tmp112Sensor temperature_;
    ClimateReportPolicy reportPolicy_;
    RollingKeepAlive reportSchedule_;
};

}  // namespace node
}  // namespace radiosensors
