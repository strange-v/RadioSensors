#pragma once

#include <stddef.h>
#include <stdint.h>

namespace radiosensors::stream {

constexpr uint8_t kVersion = 1;
constexpr size_t kControlFrameSize = 6;
constexpr size_t kTelemetryEnvelopeSize = 20;

enum class MessageKind : uint8_t {
    SnapshotBegin = 1,
    Telemetry = 2,
    SnapshotEnd = 3,
};

inline void writeUint16(uint8_t* const output, const uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
}

inline void writeUint32(uint8_t* const output, const uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
    output[2] = static_cast<uint8_t>(value >> 16U);
    output[3] = static_cast<uint8_t>(value >> 24U);
}

inline void writeUint64(uint8_t* const output, const uint64_t value) {
    writeUint32(output, static_cast<uint32_t>(value));
    writeUint32(output + 4, static_cast<uint32_t>(value >> 32U));
}

inline size_t encodeControl(
    const MessageKind kind,
    const uint32_t sequence,
    uint8_t* const output,
    const size_t capacity) {
    if (output == nullptr || capacity < kControlFrameSize ||
        (kind != MessageKind::SnapshotBegin && kind != MessageKind::SnapshotEnd)) {
        return 0;
    }
    output[0] = kVersion;
    output[1] = static_cast<uint8_t>(kind);
    writeUint32(output + 2, sequence);
    return kControlFrameSize;
}

inline size_t encodeTelemetry(
    const uint32_t sequence,
    const uint8_t nodeId,
    const uint16_t profileId,
    const uint64_t receivedAtUnixMs,
    const int16_t rssi,
    const uint8_t* const payload,
    const size_t payloadSize,
    uint8_t* const output,
    const size_t capacity) {
    const size_t requiredSize = kTelemetryEnvelopeSize + payloadSize;
    if (output == nullptr || capacity < requiredSize ||
        payloadSize > UINT8_MAX || (payloadSize != 0 && payload == nullptr)) {
        return 0;
    }
    output[0] = kVersion;
    output[1] = static_cast<uint8_t>(MessageKind::Telemetry);
    writeUint32(output + 2, sequence);
    output[6] = nodeId;
    writeUint16(output + 7, profileId);
    writeUint64(output + 9, receivedAtUnixMs);
    writeUint16(output + 17, static_cast<uint16_t>(rssi));
    output[19] = static_cast<uint8_t>(payloadSize);
    for (size_t index = 0; index < payloadSize; ++index) {
        output[kTelemetryEnvelopeSize + index] = payload[index];
    }
    return requiredSize;
}

}  // namespace radiosensors::stream
