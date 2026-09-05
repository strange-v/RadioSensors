#pragma once

#include <TelemetryFrames.h>
#include <stddef.h>
#include <stdint.h>

#include "BatteryMonitor.h"
#include "Tmp112Sensor.h"

namespace radiosensors {
namespace node {

class ClimateTmp112Profile {
public:
    ClimateTmp112Profile(TwoWire& wire, uint8_t address)
        : temperature_(wire, address) {}

    void begin() { temperature_.begin(); }

    size_t encodeTelemetry(uint8_t* output, const size_t capacity) {
        int16_t temperature = protocol::kInvalidTemperature;
        temperature_.readTemperature(temperature);
        const uint16_t voltage = battery_.readMillivolts();
        return protocol::encodeTemperatureTelemetry(
                   temperature, voltage, output, capacity) ==
                protocol::TelemetryCodecStatus::Ok
            ? protocol::kTemperatureTelemetrySize
            : 0;
    }

private:
    Tmp112Sensor temperature_;
    BatteryMonitor battery_;
};

}  // namespace node
}  // namespace radiosensors
