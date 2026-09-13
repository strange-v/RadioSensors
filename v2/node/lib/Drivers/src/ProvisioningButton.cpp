#include "ProvisioningButton.h"

#include <avr/interrupt.h>

namespace radiosensors {
namespace node {

namespace {
constexpr uint8_t kDebounceMs = 20;
}

volatile bool ProvisioningButton::edgePending_ = false;

void ProvisioningButton::onEdge() {
    edgePending_ = true;
}

void ProvisioningButton::begin() {
    pinMode(pin_, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin_), onEdge, CHANGE);
}

ButtonGesture ProvisioningButton::takeGesture() {
    const uint8_t status = SREG;
    cli();
    const bool edgePending = edgePending_;
    edgePending_ = false;
    SREG = status;
    if (!edgePending || !settledAt(LOW)) return ButtonGesture::None;

#if defined(NODE_DEBUG)
    Serial.println(F("button: pressed"));
#endif
    const uint32_t pressedAt = millis();
    while (true) {
        const uint32_t heldMs = millis() - pressedAt;
        if (heldMs >= kLongPressMs) {
#if defined(NODE_DEBUG)
            Serial.print(F("button: held for "));
            Serial.print(kLongPressMs / 1000UL);
            Serial.println(F(" s"));
#endif
            return ButtonGesture::LongPress;
        }
        if (digitalRead(pin_) != LOW && settledAt(HIGH)) {
#if defined(NODE_DEBUG)
            Serial.print(F("button: released after "));
            Serial.print(heldMs);
            Serial.println(F(" ms"));
#endif
            return ButtonGesture::ShortPress;
        }
    }
}

bool ProvisioningButton::settledAt(const uint8_t level) const {
    if (digitalRead(pin_) != level) return false;
    delay(kDebounceMs);
    return digitalRead(pin_) == level;
}

}  // namespace node
}  // namespace radiosensors
