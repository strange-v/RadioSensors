#pragma once

#include <stdint.h>

namespace radiosensors {
namespace node {

class LowPowerClock {
public:
    // Uses the ATtiny1614 RTC PIT at 32-second intervals. Time is deliberately
    // tracked outside millis(), whose timer stops in power-down sleep.
    void begin();
    uint32_t nowMs() const;
    void sleepUntilInterrupt() const;
};

}  // namespace node
}  // namespace radiosensors
