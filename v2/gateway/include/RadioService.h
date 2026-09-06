#pragma once

#include <Arduino.h>

namespace gateway::radio {

constexpr size_t kMaxPayloadSize = 61;

enum class Profile {
    Operational,
    Commissioning,
};

struct ReceivedFrame {
    uint8_t data[kMaxPayloadSize];
    uint8_t size;
    uint16_t senderId;
    int16_t rssi;
    bool ackRequested;
    uint32_t receivedAtMs;
};

enum class State {
    Stopped,
    Starting,
    SpiInitializationFailed,
    InitializationFailed,
    VersionMismatch,
    EncryptionKeyMissing,
    TaskFailed,
    Receiving,
};

struct Snapshot {
    State state;
    uint8_t version;
    uint32_t frequencyHz;
    uint32_t bitRate;
    int8_t configuredPowerDbm;
    bool encryptionEnabled;
    uint32_t interrupts;
    uint32_t packets;
    uint32_t bytes;
    uint32_t emptyWakeups;
    uint32_t ackRequestsIgnored;
    uint32_t telemetryAcksSent;
    uint32_t telemetryRejectedInactive;
    uint32_t telemetryFramesQueued;
    uint32_t telemetryFramesDropped;
    uint32_t v2Frames;
    uint32_t v2TelemetryFrames;
    uint32_t emptyApplicationFrames;
    uint32_t unsupportedProtocolVersions;
    uint32_t unsupportedFrameKinds;
    uint32_t rxFramesQueued;
    uint32_t rxFramesDropped;
    uint32_t commandsQueued;
    uint32_t commandsDropped;
    uint32_t commandsProcessed;
    Profile profile;
    uint8_t currentNetworkId;
    uint32_t lastPacketMs;
    uint16_t lastSenderId;
    int16_t lastRssi;
};

bool begin();
bool receive(ReceivedFrame& frame, TickType_t waitTicks = 0);
bool receiveTelemetry(ReceivedFrame& frame, TickType_t waitTicks = 0);
bool requestProfile(Profile profile);
bool send(uint16_t targetId, const uint8_t* data, size_t size, bool requestAck = false);
bool sendThenSwitchProfile(
    uint16_t targetId,
    const uint8_t* data,
    size_t size,
    Profile profile);
State state();
const char* stateName();
const char* frequencyBandName();
const char* spiHostName();
const char* profileName();
Snapshot snapshot();

}  // namespace gateway::radio
