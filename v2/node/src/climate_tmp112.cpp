#include <Arduino.h>
#include <Wire.h>

#include "CommissioningService.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "NodeRuntime.h"
#include "ProvisioningButton.h"
#include "Profiles/ClimateTmp112Profile.h"
#include "UnusedPins.h"

namespace {
using namespace radiosensors;
using Profile = node::ClimateTmp112Profile;

constexpr protocol::FirmwareVersion kFirmwareVersion{0, 1, 0};

// PA5 is the reed pad; PB2/PB3 carry UART only in debug builds.
constexpr uint8_t kUnusedPins[] = {
    PIN_PA5,
#if !defined(NODE_DEBUG)
    PIN_PB2,
    PIN_PB3,
#endif
};

#if defined(NODE_CLIMATE_ADAPTIVE_REPORTING)
const node::ClimateReportPolicy reportPolicy =
    node::ClimateReportPolicy::adaptive(
        NODE_CLIMATE_CHARGED_REPORT_INTERVAL_MS,
        NODE_CLIMATE_LOW_CHARGE_REPORT_INTERVAL_MS,
        NODE_CLIMATE_LOW_CHARGE_THRESHOLD_MV);
#else
const node::ClimateReportPolicy reportPolicy =
    node::ClimateReportPolicy::fixed(NODE_CLIMATE_REPORT_INTERVAL_MS);
#endif

node::NodeRadio radio(NODE_RFM69_CS, NODE_RFM69_IRQ);
node::LowPowerClock clock;
node::ProvisioningButton button(NODE_BUTTON_PIN);
Profile profile(Wire, NODE_TMP112_ADDRESS, reportPolicy);
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
    node::disableUnusedPins(kUnusedPins);
    runtime.begin();
}

void loop() {
    runtime.runOnce();
}
