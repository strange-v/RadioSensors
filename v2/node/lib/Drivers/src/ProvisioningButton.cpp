#include "ProvisioningButton.h"

#include <avr/interrupt.h>

namespace radiosensors {
namespace node {

volatile bool ProvisioningButton::edgePending_ = false;

void ProvisioningButton::onEdge() {
    edgePending_ = true;
}

void ProvisioningButton::begin() {
    pinMode(pin_, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin_), onEdge, CHANGE);
}

bool ProvisioningButton::consumePress() {
    const uint8_t status = SREG;
    cli();
    const bool edgePending = edgePending_;
    edgePending_ = false;
    SREG = status;
    if (!edgePending) return false;

    if (digitalRead(pin_) != LOW) {
        pressLatched_ = false;
        return false;
    }
    if (pressLatched_) return false;

    delay(20);
    if (digitalRead(pin_) != LOW) return false;
    pressLatched_ = true;
    return true;
}

}  // namespace node
}  // namespace radiosensors
