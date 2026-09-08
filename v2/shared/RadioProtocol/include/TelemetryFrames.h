#pragma once

#include <stddef.h>
#include <stdint.h>

#include "JoinRequest.h"

namespace radiosensors {
namespace protocol {

constexpr int16_t kInvalidTemperature = INT16_MIN;
constexpr uint16_t kInvalidHumidity = UINT16_MAX;
constexpr uint16_t kInvalidPressure = UINT16_MAX;
constexpr uint16_t kInvalidSupplyVoltage = UINT16_MAX;

constexpr int16_t kMinimumTemperatureCentiDegrees = -8000;
constexpr int16_t kMaximumTemperatureCentiDegrees = 12500;
constexpr uint16_t kMaximumHumidityCentiPercent = 10000;
constexpr uint16_t kMinimumPressureDeciHectopascals = 3000;
constexpr uint16_t kMaximumPressureDeciHectopascals = 11000;

constexpr size_t kTelemetryPrefixSize = 3;
constexpr size_t kVoltageTelemetrySize = 3;
constexpr size_t kTemperatureTelemetrySize = 5;
constexpr size_t kClimateThTelemetrySize = 7;
constexpr size_t kClimateThpTelemetrySize = 9;
constexpr size_t kBinaryTelemetrySize = 4;
constexpr size_t kCounterTelemetrySize = 7;
constexpr size_t kBinaryClimateThTelemetrySize = 8;
constexpr size_t kBinaryTemperatureTelemetrySize = 6;

enum class TelemetryCodecStatus : uint8_t {
    Ok,
    OutputTooSmall,
    WrongLength,
    WrongHeader,
    InvalidState,
    InvalidTemperature,
    InvalidHumidity,
    InvalidPressure,
};

struct TelemetryView {
    uint16_t supplyMillivolts;
    const uint8_t* profilePayload;
    size_t profilePayloadSize;
};

inline bool validTemperature(const int16_t value) {
    return value == kInvalidTemperature ||
        (value >= kMinimumTemperatureCentiDegrees &&
         value <= kMaximumTemperatureCentiDegrees);
}

inline bool validHumidity(const uint16_t value) {
    return value == kInvalidHumidity || value <= kMaximumHumidityCentiPercent;
}

inline bool validPressure(const uint16_t value) {
    return value == kInvalidPressure ||
        (value >= kMinimumPressureDeciHectopascals &&
         value <= kMaximumPressureDeciHectopascals);
}

inline TelemetryCodecStatus beginTelemetry(
    const uint16_t supplyMillivolts, uint8_t* const output,
    const size_t capacity, const size_t required) {
    if (output == nullptr || capacity < required) {
        return TelemetryCodecStatus::OutputTooSmall;
    }
    output[0] = encodeHeader(FrameKind::Telemetry);
    writeUint16Le(output + 1, supplyMillivolts);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus decodeTelemetry(
    const uint8_t* const input, const size_t size, TelemetryView& view) {
    if (input == nullptr || size < kTelemetryPrefixSize) {
        return TelemetryCodecStatus::WrongLength;
    }
    if (input[0] != encodeHeader(FrameKind::Telemetry)) {
        return TelemetryCodecStatus::WrongHeader;
    }
    view.supplyMillivolts = readUint16Le(input + 1);
    view.profilePayload = input + kTelemetryPrefixSize;
    view.profilePayloadSize = size - kTelemetryPrefixSize;
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeVoltageTelemetry(
    const uint16_t vccMillivolts, uint8_t* const output,
    const size_t capacity) {
    return beginTelemetry(
        vccMillivolts, output, capacity, kVoltageTelemetrySize);
}

inline TelemetryCodecStatus encodeTemperatureTelemetry(
    const uint16_t vccMillivolts, const int16_t temperatureCentiDegrees,
    uint8_t* const output, const size_t capacity) {
    if (!validTemperature(temperatureCentiDegrees)) {
        return TelemetryCodecStatus::InvalidTemperature;
    }
    const auto status = beginTelemetry(
        vccMillivolts, output, capacity, kTemperatureTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    writeUint16Le(output + 3, static_cast<uint16_t>(temperatureCentiDegrees));
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeClimateThTelemetry(
    const uint16_t vccMillivolts, const int16_t temperatureCentiDegrees,
    const uint16_t humidityCentiPercent, uint8_t* const output,
    const size_t capacity) {
    if (!validTemperature(temperatureCentiDegrees)) {
        return TelemetryCodecStatus::InvalidTemperature;
    }
    if (!validHumidity(humidityCentiPercent)) {
        return TelemetryCodecStatus::InvalidHumidity;
    }
    const auto status = beginTelemetry(
        vccMillivolts, output, capacity, kClimateThTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    writeUint16Le(output + 3, static_cast<uint16_t>(temperatureCentiDegrees));
    writeUint16Le(output + 5, humidityCentiPercent);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeClimateThpTelemetry(
    const uint16_t vccMillivolts, const int16_t temperatureCentiDegrees,
    const uint16_t humidityCentiPercent,
    const uint16_t pressureDeciHectopascals, uint8_t* const output,
    const size_t capacity) {
    if (!validTemperature(temperatureCentiDegrees)) {
        return TelemetryCodecStatus::InvalidTemperature;
    }
    if (!validHumidity(humidityCentiPercent)) {
        return TelemetryCodecStatus::InvalidHumidity;
    }
    if (!validPressure(pressureDeciHectopascals)) {
        return TelemetryCodecStatus::InvalidPressure;
    }
    const auto status = beginTelemetry(
        vccMillivolts, output, capacity, kClimateThpTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    writeUint16Le(output + 3, static_cast<uint16_t>(temperatureCentiDegrees));
    writeUint16Le(output + 5, humidityCentiPercent);
    writeUint16Le(output + 7, pressureDeciHectopascals);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeBinaryTelemetry(
    const uint16_t vccMillivolts, const uint8_t state,
    uint8_t* const output, const size_t capacity) {
    if (state > 1) return TelemetryCodecStatus::InvalidState;
    const auto status = beginTelemetry(
        vccMillivolts, output, capacity, kBinaryTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    output[3] = state;
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeCounterTelemetry(
    const uint16_t vccMillivolts, const uint32_t count,
    uint8_t* const output, const size_t capacity) {
    const auto status = beginTelemetry(
        vccMillivolts, output, capacity, kCounterTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    writeUint32Le(output + 3, count);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeBinaryClimateThTelemetry(
    const uint16_t vccMillivolts, const uint8_t state,
    const int16_t temperatureCentiDegrees,
    const uint16_t humidityCentiPercent,
    uint8_t* const output, const size_t capacity) {
    if (state > 1) return TelemetryCodecStatus::InvalidState;
    if (!validTemperature(temperatureCentiDegrees)) {
        return TelemetryCodecStatus::InvalidTemperature;
    }
    if (!validHumidity(humidityCentiPercent)) {
        return TelemetryCodecStatus::InvalidHumidity;
    }
    const auto status = beginTelemetry(
        vccMillivolts, output, capacity, kBinaryClimateThTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    output[3] = state;
    writeUint16Le(output + 4, static_cast<uint16_t>(temperatureCentiDegrees));
    writeUint16Le(output + 6, humidityCentiPercent);
    return TelemetryCodecStatus::Ok;
}

inline TelemetryCodecStatus encodeBinaryTemperatureTelemetry(
    const uint16_t vccMillivolts, const uint8_t state,
    const int16_t temperatureCentiDegrees, uint8_t* const output,
    const size_t capacity) {
    if (state > 1) return TelemetryCodecStatus::InvalidState;
    if (!validTemperature(temperatureCentiDegrees)) {
        return TelemetryCodecStatus::InvalidTemperature;
    }
    const auto status = beginTelemetry(
        vccMillivolts, output, capacity, kBinaryTemperatureTelemetrySize);
    if (status != TelemetryCodecStatus::Ok) return status;
    output[3] = state;
    writeUint16Le(output + 4, static_cast<uint16_t>(temperatureCentiDegrees));
    return TelemetryCodecStatus::Ok;
}

}  // namespace protocol
}  // namespace radiosensors
