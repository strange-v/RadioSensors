#pragma once

#include "TelemetryStore.h"

namespace gateway::health {

void begin();
void loop();
void publishTelemetry(const telemetry_store::Record& record);

}  // namespace gateway::health
