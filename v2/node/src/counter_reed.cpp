#include <Arduino.h>

#include "CommissioningService.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "NodeRuntime.h"
#include "ProvisioningButton.h"
#include "Profiles/CounterReedProfile.h"

namespace {
using namespace radiosensors;
using Profile = node::CounterReedProfile;

constexpr protocol::FirmwareVersion kFirmwareVersion{0, 1, 0};

node::NodeRadio radio(NODE_RFM69_CS, NODE_RFM69_IRQ);
node::LowPowerClock clock;
node::ProvisioningButton button(NODE_BUTTON_PIN);
Profile profile(NODE_REED_PIN, NODE_COUNTER_MINIMUM_PHASE_MS);
node::CommissioningService commissioning(
    radio, Profile::kProfileId, kFirmwareVersion);
node::NodeRuntime<Profile> runtime(
    profile, radio, commissioning, clock, button);
}

void setup() {
#if defined(NODE_DEBUG)
    Serial.begin(9600);
    Serial.println(F("node: startup"));
#endif
    runtime.begin();
}

void loop() {
    runtime.runOnce();
}
