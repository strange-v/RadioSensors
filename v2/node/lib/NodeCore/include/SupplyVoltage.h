#pragma once

#include <stdint.h>

namespace radiosensors {
namespace node {

// Sleep current is too small to reveal battery sag, and the radio burst cannot
// be sampled while sendWithRetry() blocks. The reported value is the lower of
// the pre-transmission measurement and the one taken immediately after the
// previous transmission, while the cell is still recovering.
class LoadedSupplyVoltage {
public:
    uint16_t report(const uint16_t beforeTransmission) const {
        return beforeTransmission < afterPrevious_
            ? beforeTransmission
            : afterPrevious_;
    }

    void transmitted(const uint16_t afterTransmission) {
        afterPrevious_ = afterTransmission;
    }

private:
    uint16_t afterPrevious_ = 0xFFFFU;
};

}  // namespace node
}  // namespace radiosensors
