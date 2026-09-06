#pragma once

#include <Arduino.h>

namespace gateway::status {

enum class Indication {
    Operational,
    Pairing,
    PersistingNode,
    AwaitingConfirm,
    PairingSucceeded,
    Error,
    ProfileConflict,
};

void begin();
void loop();
bool pairingActive();
uint32_t pairingRemainingSeconds();
bool closePairing();
void indicate(Indication indication, uint32_t durationMs = 0);
const char* indicationName();

}  // namespace gateway::status
