#pragma once

#include <Arduino.h>

namespace gateway::commissioning {

struct Snapshot {
    uint32_t joinRequests;
    uint32_t joinAcceptsQueued;
    uint32_t joinConfirms;
    uint32_t nodesActivated;
    uint32_t joinCompletesQueued;
    uint32_t rejectedFrames;
    uint32_t storageErrors;
    uint32_t confirmTimeouts;
};

bool begin();
Snapshot snapshot();

}  // namespace gateway::commissioning
