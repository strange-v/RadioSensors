#pragma once

#include <stddef.h>
#include <stdint.h>

#include "JoinRequest.h"

namespace radiosensors {
namespace protocol {

constexpr int16_t kInvalidTemperature = INT16_MIN;
constexpr uint16_t kInvalidHumidity = UINT16_MAX;

constexpr size_t kVoltageTelemetrySize = 3;
constexpr size_t kTemperatureTelemetrySize = 5;
constexpr size_t kBinaryTelemetrySize = 4;
constexpr size_t kCounterTelemetrySize = 7;
constexpr size_t kBinaryClimateThTelemetrySize = 8;
constexpr size_t kBinaryTemperatureTelemetrySize = 6;

enum class TelemetryCodecStatus : uint8_t {
    Ok,
    OutputTooSmall,
    InvalidState,
};

inline TelemetryCodecStatus beginTelemetry(
    uint8_t* const output, const size_t capacity, const size_t required) {
    if (output == nullptr || capacity < required) {
        return TelemetryCodecStatus::OutputTooSmall;
    }
    output[0] = encodeHeader(FrameKind::Telemetry);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeVoltageTelemetry(
    const uint16_t vccMillivolts, uint8_t* const output,
    const size_t capacity) {
    const auto status = beginTelemetry(output, capacity, kVoltageTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    writeUint16Le(output + 1, vccMillivolts);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeTemperatureTelemetry(
    const int16_t temperatureCentiDegrees, const uint16_t vccMillivolts,
    uint8_t* const output, const size_t capacity) {
    const auto status =
        beginTelemetry(output, capacity, kTemperatureTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    writeUint16Le(output + 1, static_cast<uint16_t>(temperatureCentiDegrees));
    writeUint16Le(output + 3, vccMillivolts);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeBinaryTelemetry(
    const uint8_t state, const uint16_t vccMillivolts,
    uint8_t* const output, const size_t capacity) {
    if (state > 1) return TelemetryCodecStatus::InvalidState;
    const auto status = beginTelemetry(output, capacity, kBinaryTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    output[1] = state;
    writeUint16Le(output + 2, vccMillivolts);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeCounterTelemetry(
    const uint32_t count, const uint16_t vccMillivolts,
    uint8_t* const output, const size_t capacity) {
    const auto status = beginTelemetry(output, capacity, kCounterTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    writeUint32Le(output + 1, count);
    writeUint16Le(output + 5, vccMillivolts);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeBinaryClimateThTelemetry(
    const uint8_t state, const int16_t temperatureCentiDegrees,
    const uint16_t humidityCentiPercent, const uint16_t vccMillivolts,
    uint8_t* const output, const size_t capacity) {
    if (state > 1) return TelemetryCodecStatus::InvalidState;
    const auto status =
        beginTelemetry(output, capacity, kBinaryClimateThTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    output[1] = state;
    writeUint16Le(output + 2, static_cast<uint16_t>(temperatureCentiDegrees));
    writeUint16Le(output + 4, humidityCentiPercent);
    writeUint16Le(output + 6, vccMillivolts);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeBinaryTemperatureTelemetry(
    const uint8_t state, const int16_t temperatureCentiDegrees,
    const uint16_t vccMillivolts, uint8_t* const output,
    const size_t capacity) {
    if (state > 1) return TelemetryCodecStatus::InvalidState;
    const auto status =
        beginTelemetry(output, capacity, kBinaryTemperatureTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    output[1] = state;
    writeUint16Le(output + 2, static_cast<uint16_t>(temperatureCentiDegrees));
    writeUint16Le(output + 4, vccMillivolts);
    return TelemetryCodecStatus::Ok;
}

}  // namespace protocol
}  // namespace radiosensors
