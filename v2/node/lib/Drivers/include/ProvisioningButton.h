#pragma once

#include <Arduino.h>

namespace radiosensors {
namespace node {

class ProvisioningButton {
public:
    explicit ProvisioningButton(const uint8_t pin) : pin_(pin) {}

    void begin();
    bool consumePress();

private:
    static void onEdge();

    uint8_t pin_;
    bool pressLatched_ = false;
    static volatile bool edgePending_;
};

}  // namespace node
}  // namespace radiosensors
