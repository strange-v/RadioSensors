#pragma once

#include <stddef.h>
#include <stdint.h>

#include "JoinRequest.h"

namespace radiosensors {
namespace protocol {

constexpr size_t kCommandReadySize = 5;
constexpr size_t kNoCommandSize = 5;

enum class CommandSessionCodecStatus : uint8_t {
    Ok,
    OutputTooSmall,
    EmptyFrame,
    UnsupportedVersion,
    UnsupportedKind,
    WrongFrameKind,
    WrongLength,
};

inline CommandSessionCodecStatus mapCommandSessionFrameStatus(
    const DecodeStatus status) {
    switch (status) {
        case DecodeStatus::EmptyFrame:
            return CommandSessionCodecStatus::EmptyFrame;
        case DecodeStatus::UnsupportedVersion:
            return CommandSessionCodecStatus::UnsupportedVersion;
        case DecodeStatus::UnsupportedKind:
            return CommandSessionCodecStatus::UnsupportedKind;
        case DecodeStatus::Ok:
            return CommandSessionCodecStatus::Ok;
    }
    return CommandSessionCodecStatus::UnsupportedKind;
}

inline CommandSessionCodecStatus encodeCommandSessionFrame(
    const FrameKind kind, const uint32_t sessionNonce,
    uint8_t* const output, const size_t capacity) {
    if (output == nullptr || capacity < kCommandReadySize) {
        return CommandSessionCodecStatus::OutputTooSmall;
    }
    output[0] = encodeHeader(kind);
    writeUint32Le(output + 1, sessionNonce);
    return CommandSessionCodecStatus::Ok;
}

inline CommandSessionCodecStatus decodeCommandSessionFrame(
    const uint8_t* const data, const size_t size, const FrameKind expectedKind,
    uint32_t& sessionNonce) {
    FrameView frame{};
    const DecodeStatus frameStatus = decodeFrame(data, size, frame);
    if (frameStatus != DecodeStatus::Ok) {
        return mapCommandSessionFrameStatus(frameStatus);
    }
    if (frame.kind != expectedKind) {
        return CommandSessionCodecStatus::WrongFrameKind;
    }
    if (size != kCommandReadySize) {
        return CommandSessionCodecStatus::WrongLength;
    }
    sessionNonce = readUint32Le(data + 1);
    return CommandSessionCodecStatus::Ok;
}

inline CommandSessionCodecStatus encodeCommandReady(
    const uint32_t sessionNonce, uint8_t* const output,
    const size_t capacity) {
    return encodeCommandSessionFrame(
        FrameKind::CommandReady, sessionNonce, output, capacity);
}

inline CommandSessionCodecStatus decodeCommandReady(
    const uint8_t* const data, const size_t size, uint32_t& sessionNonce) {
    return decodeCommandSessionFrame(
        data, size, FrameKind::CommandReady, sessionNonce);
}

inline CommandSessionCodecStatus encodeNoCommand(
    const uint32_t sessionNonce, uint8_t* const output,
    const size_t capacity) {
    return encodeCommandSessionFrame(
        FrameKind::NoCommand, sessionNonce, output, capacity);
}

inline CommandSessionCodecStatus decodeNoCommand(
    const uint8_t* const data, const size_t size, uint32_t& sessionNonce) {
    return decodeCommandSessionFrame(
        data, size, FrameKind::NoCommand, sessionNonce);
}

}  // namespace protocol
}  // namespace radiosensors
