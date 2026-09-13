#pragma once

#include <stddef.h>
#include <stdint.h>

#include "JoinRequest.h"
#include "ProfileIds.h"

namespace radiosensors {
namespace protocol {

constexpr size_t kCommandReadySize = 5;
constexpr size_t kNoCommandSize = 5;
constexpr size_t kCommandEnvelopeSize = 8;
constexpr size_t kMaxCommandArgumentSize = 8;
constexpr size_t kMaxCommandResultDataSize = 8;
constexpr size_t kMaxCommandSize =
    kCommandEnvelopeSize + kMaxCommandArgumentSize;
constexpr size_t kMaxCommandResultSize =
    kCommandEnvelopeSize + kMaxCommandResultDataSize;

constexpr size_t kSetRadioPowerArgumentSize = 1;
constexpr uint8_t kMaxRadioPowerLevel = 31;
constexpr size_t kSetCountArgumentSize = 4;
constexpr size_t kSetCountResultSize = 8;

// Payload of the RFM69 ACK that answers telemetry: empty, or one flags byte.
constexpr uint8_t kTelemetryAckCommandPending = 0x01;

enum class CommandType : uint8_t {
    SetRadioPower = 1,
    SetCount = 2,
};

enum class CommandStatus : uint8_t {
    Applied = 0,
    Unsupported = 1,
    InvalidArgument = 2,
    StorageFailure = 3,
};

// The type stays raw so a node can answer a type it does not know with
// Unsupported instead of dropping the frame.
struct Command {
    uint32_t sessionNonce;
    uint16_t commandId;
    uint8_t type;
    uint8_t argumentSize;
    uint8_t arguments[kMaxCommandArgumentSize];
};

struct CommandResult {
    uint32_t sessionNonce;
    uint16_t commandId;
    CommandStatus status;
    uint8_t dataSize;
    uint8_t data[kMaxCommandResultDataSize];
};

enum class CommandSessionCodecStatus : uint8_t {
    Ok,
    OutputTooSmall,
    EmptyFrame,
    UnsupportedVersion,
    UnsupportedKind,
    WrongFrameKind,
    WrongLength,
    InvalidCommandId,
    InvalidStatus,
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

inline size_t commandFrameSize(const Command& command) {
    return kCommandEnvelopeSize + command.argumentSize;
}

inline size_t commandResultFrameSize(const CommandResult& result) {
    return kCommandEnvelopeSize + result.dataSize;
}

inline CommandSessionCodecStatus encodeCommand(
    const Command& command, uint8_t* const output, const size_t capacity) {
    if (command.commandId == 0) {
        return CommandSessionCodecStatus::InvalidCommandId;
    }
    if (command.argumentSize > kMaxCommandArgumentSize) {
        return CommandSessionCodecStatus::WrongLength;
    }
    if (output == nullptr || capacity < commandFrameSize(command)) {
        return CommandSessionCodecStatus::OutputTooSmall;
    }
    output[0] = encodeHeader(FrameKind::Command);
    writeUint32Le(output + 1, command.sessionNonce);
    writeUint16Le(output + 5, command.commandId);
    output[7] = command.type;
    for (size_t index = 0; index < command.argumentSize; ++index) {
        output[kCommandEnvelopeSize + index] = command.arguments[index];
    }
    return CommandSessionCodecStatus::Ok;
}

inline CommandSessionCodecStatus decodeCommand(
    const uint8_t* const data, const size_t size, Command& command) {
    FrameView frame{};
    const DecodeStatus frameStatus = decodeFrame(data, size, frame);
    if (frameStatus != DecodeStatus::Ok) {
        return mapCommandSessionFrameStatus(frameStatus);
    }
    if (frame.kind != FrameKind::Command) {
        return CommandSessionCodecStatus::WrongFrameKind;
    }
    if (size < kCommandEnvelopeSize || size > kMaxCommandSize) {
        return CommandSessionCodecStatus::WrongLength;
    }
    const uint16_t commandId = readUint16Le(data + 5);
    if (commandId == 0) return CommandSessionCodecStatus::InvalidCommandId;
    command.sessionNonce = readUint32Le(data + 1);
    command.commandId = commandId;
    command.type = data[7];
    command.argumentSize = static_cast<uint8_t>(size - kCommandEnvelopeSize);
    for (size_t index = 0; index < command.argumentSize; ++index) {
        command.arguments[index] = data[kCommandEnvelopeSize + index];
    }
    return CommandSessionCodecStatus::Ok;
}

inline bool validCommandStatus(const uint8_t status) {
    return status <= static_cast<uint8_t>(CommandStatus::StorageFailure);
}

inline CommandSessionCodecStatus encodeCommandResult(
    const CommandResult& result, uint8_t* const output,
    const size_t capacity) {
    if (result.commandId == 0) {
        return CommandSessionCodecStatus::InvalidCommandId;
    }
    if (!validCommandStatus(static_cast<uint8_t>(result.status))) {
        return CommandSessionCodecStatus::InvalidStatus;
    }
    if (result.dataSize > kMaxCommandResultDataSize) {
        return CommandSessionCodecStatus::WrongLength;
    }
    if (output == nullptr || capacity < commandResultFrameSize(result)) {
        return CommandSessionCodecStatus::OutputTooSmall;
    }
    output[0] = encodeHeader(FrameKind::CommandResult);
    writeUint32Le(output + 1, result.sessionNonce);
    writeUint16Le(output + 5, result.commandId);
    output[7] = static_cast<uint8_t>(result.status);
    for (size_t index = 0; index < result.dataSize; ++index) {
        output[kCommandEnvelopeSize + index] = result.data[index];
    }
    return CommandSessionCodecStatus::Ok;
}

inline CommandSessionCodecStatus decodeCommandResult(
    const uint8_t* const data, const size_t size, CommandResult& result) {
    FrameView frame{};
    const DecodeStatus frameStatus = decodeFrame(data, size, frame);
    if (frameStatus != DecodeStatus::Ok) {
        return mapCommandSessionFrameStatus(frameStatus);
    }
    if (frame.kind != FrameKind::CommandResult) {
        return CommandSessionCodecStatus::WrongFrameKind;
    }
    if (size < kCommandEnvelopeSize || size > kMaxCommandResultSize) {
        return CommandSessionCodecStatus::WrongLength;
    }
    const uint16_t commandId = readUint16Le(data + 5);
    if (commandId == 0) return CommandSessionCodecStatus::InvalidCommandId;
    if (!validCommandStatus(data[7])) {
        return CommandSessionCodecStatus::InvalidStatus;
    }
    result.sessionNonce = readUint32Le(data + 1);
    result.commandId = commandId;
    result.status = static_cast<CommandStatus>(data[7]);
    result.dataSize = static_cast<uint8_t>(size - kCommandEnvelopeSize);
    for (size_t index = 0; index < result.dataSize; ++index) {
        result.data[index] = data[kCommandEnvelopeSize + index];
    }
    return CommandSessionCodecStatus::Ok;
}

// Argument size of a known command type; false for an unknown type.
inline bool commandArgumentSize(const uint8_t type, size_t& size) {
    switch (static_cast<CommandType>(type)) {
        case CommandType::SetRadioPower:
            size = kSetRadioPowerArgumentSize;
            return true;
        case CommandType::SetCount:
            size = kSetCountArgumentSize;
            return true;
    }
    return false;
}

inline bool validCommandArguments(
    const uint8_t type, const uint8_t* const arguments, const size_t size) {
    size_t expected = 0;
    if (!commandArgumentSize(type, expected) || size != expected ||
        arguments == nullptr) {
        return false;
    }
    return static_cast<CommandType>(type) != CommandType::SetRadioPower ||
        arguments[0] <= kMaxRadioPowerLevel;
}

// Result data accompanies only an applied command.
inline size_t commandResultDataSize(
    const CommandType type, const CommandStatus status) {
    if (status != CommandStatus::Applied) return 0;
    return type == CommandType::SetCount ? kSetCountResultSize : 0;
}

inline bool profileSupportsCommand(
    const uint16_t profileId, const CommandType type) {
    switch (type) {
        case CommandType::SetRadioPower:
            return profileId >= profileIdValue(ProfileId::Voltage) &&
                profileId <= profileIdValue(ProfileId::BinaryTemperature);
        case CommandType::SetCount:
            return profileId == profileIdValue(ProfileId::PulseCounter);
    }
    return false;
}

inline bool telemetryAckCommandPending(
    const uint8_t* const data, const size_t size) {
    return data != nullptr && size != 0 &&
        (data[0] & kTelemetryAckCommandPending) != 0;
}

}  // namespace protocol
}  // namespace radiosensors
