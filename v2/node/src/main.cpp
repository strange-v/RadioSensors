#include <Arduino.h>

#if defined(NODE_BUILD_CLIMATE_TMP112)

#include <ProfileIds.h>
#include <Wire.h>

#include "ClimateNodeApplication.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "Profiles/ClimateTmp112Profile.h"

#ifndef NODE_COMMISSIONING_KEY
#error "NODE_COMMISSIONING_KEY must be an exactly 16-byte string"
#endif

static_assert(
    sizeof(NODE_COMMISSIONING_KEY) == 17,
    "NODE_COMMISSIONING_KEY must be exactly 16 bytes");

namespace {
using namespace radiosensors;

constexpr protocol::FirmwareVersion kFirmwareVersion{0, 1, 0};
constexpr uint16_t kProfileId =
    protocol::profileIdValue(protocol::ProfileId::Temperature);

node::NodeRadio radio(NODE_RFM69_CS, NODE_RFM69_IRQ);
node::LowPowerClock clock;
node::ClimateTmp112Profile profile(Wire, NODE_TMP112_ADDRESS);
node::CommissioningService commissioning(
    radio, kProfileId, kFirmwareVersion, NODE_COMMISSIONING_KEY);
node::ClimateNodeApplication<node::ClimateTmp112Profile> application(
    profile, radio, commissioning, clock, NODE_CLIMATE_REPORT_INTERVAL_MS);
}

void setup() {
    application.begin();
}

void loop() {
    application.runOnce();
}

#else

#error "This profile runtime is not implemented yet"

#endif
