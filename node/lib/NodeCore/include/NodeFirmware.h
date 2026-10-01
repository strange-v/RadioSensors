#pragma once

#include <JoinRequest.h>

namespace radiosensors {
namespace node {

// One version for every node image; the gateway records it at pairing.
constexpr protocol::FirmwareVersion kFirmwareVersion{1, 0, 1};

}  // namespace node
}  // namespace radiosensors
