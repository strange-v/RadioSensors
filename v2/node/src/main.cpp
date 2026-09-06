#include <Arduino.h>

#if defined(NODE_BUILD_CLIMATE_TMP112)

#include <ProfileIds.h>
#include <Wire.h>

#include "ClimateNodeApplication.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "ProvisioningButton.h"
#include "Profiles/ClimateTmp112Profile.h"

namespace {
using namespace radiosensors;

constexpr protocol::FirmwareVersion kFirmwareVersion{0, 1, 0};
constexpr uint16_t kProfileId =
    protocol::profileIdValue(protocol::ProfileId::Temperature);

node::NodeRadio radio(NODE_RFM69_CS, NODE_RFM69_IRQ);
node::LowPowerClock clock;
node::ProvisioningButton button(NODE_BUTTON_PIN);
node::ClimateTmp112Profile profile(Wire, NODE_TMP112_ADDRESS);
node::CommissioningService commissioning(
    radio, kProfileId, kFirmwareVersion);
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
node::ClimateNodeApplication<node::ClimateTmp112Profile> application(
    profile, radio, commissioning, clock, button, reportPolicy);
}

void setup() {
#if defined(NODE_DEBUG)
    Serial.begin(9600);
    Serial.println(F("node: startup"));
#endif
    application.begin();
}

void loop() {
    application.runOnce();
}

#else

#error "This profile runtime is not implemented yet"

#endif
