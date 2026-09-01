#pragma once

#include <Arduino.h>

namespace gateway::board {

enum class EthernetController {
    Lan8720,
    W5500,
};

struct Profile {
    const char* name;
    EthernetController ethernetController;
    bool hasPoe;
};

#if defined(GATEWAY_BOARD_WT32_ETH01)

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "The WT32-ETH01 profile must be built for ESP32."
#endif

constexpr Profile current{
    "WT32-ETH01",
    EthernetController::Lan8720,
    false,
};

#elif defined(GATEWAY_BOARD_WAVESHARE_S3_ETH)

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "The Waveshare ESP32-S3-ETH profile must be built for ESP32-S3."
#endif

constexpr Profile current{
    "Waveshare ESP32-S3-ETH + PoE",
    EthernetController::W5500,
    true,
};

#else
#error "Select a supported gateway board profile."
#endif

}  // namespace gateway::board
