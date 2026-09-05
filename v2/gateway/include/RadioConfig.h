#pragma once

#include <Arduino.h>
#include <RFM69.h>
#include <SPI.h>

#if __has_include("LocalSecrets.h")
#include "LocalSecrets.h"
#endif

#ifndef GATEWAY_RFM69_ENCRYPTION_KEY
#define GATEWAY_RFM69_ENCRYPTION_KEY ""
#endif
#ifndef GATEWAY_RFM69_COMMISSIONING_KEY
#define GATEWAY_RFM69_COMMISSIONING_KEY ""
#endif
#ifndef GATEWAY_RFM69_COMMISSIONING_NETWORK_ID
#define GATEWAY_RFM69_COMMISSIONING_NETWORK_ID 0
#endif

#if !defined(GATEWAY_RFM69_SPI_HOST)
#error "Define GATEWAY_RFM69_SPI_HOST in the PlatformIO environment build_flags."
#endif
#if !defined(GATEWAY_RFM69_SCK)
#error "Define GATEWAY_RFM69_SCK in the PlatformIO environment build_flags."
#endif
#if !defined(GATEWAY_RFM69_MISO)
#error "Define GATEWAY_RFM69_MISO in the PlatformIO environment build_flags."
#endif
#if !defined(GATEWAY_RFM69_MOSI)
#error "Define GATEWAY_RFM69_MOSI in the PlatformIO environment build_flags."
#endif
#if !defined(GATEWAY_RFM69_CS)
#error "Define GATEWAY_RFM69_CS in the PlatformIO environment build_flags."
#endif
#if !defined(GATEWAY_RFM69_IRQ)
#error "Define GATEWAY_RFM69_IRQ in the PlatformIO environment build_flags."
#endif
#if !defined(GATEWAY_RFM69_FREQUENCY)
#error "Define GATEWAY_RFM69_FREQUENCY in build_flags."
#endif
#if !defined(GATEWAY_RFM69_NODE_ID)
#error "Define GATEWAY_RFM69_NODE_ID in build_flags."
#endif
#if !defined(GATEWAY_RFM69_NETWORK_ID)
#error "Define GATEWAY_RFM69_NETWORK_ID in build_flags."
#endif
#if !defined(GATEWAY_RFM69_HIGH_POWER)
#error "Define GATEWAY_RFM69_HIGH_POWER as 0 or 1 in build_flags."
#endif
#if !defined(GATEWAY_RFM69_TX_POWER_DBM)
#error "Define GATEWAY_RFM69_TX_POWER_DBM in build_flags."
#endif

static_assert(GATEWAY_RFM69_NODE_ID >= 1 && GATEWAY_RFM69_NODE_ID <= 1023,
              "RFM69 node ID must be in the range 1..1023.");
static_assert(GATEWAY_RFM69_NETWORK_ID >= 0 && GATEWAY_RFM69_NETWORK_ID <= 255,
              "RFM69 network ID must be in the range 0..255.");
static_assert(GATEWAY_RFM69_HIGH_POWER == 0 || GATEWAY_RFM69_HIGH_POWER == 1,
              "GATEWAY_RFM69_HIGH_POWER must be 0 or 1.");
static_assert(
    sizeof(GATEWAY_RFM69_ENCRYPTION_KEY) == 1 ||
        sizeof(GATEWAY_RFM69_ENCRYPTION_KEY) == 17,
    "GATEWAY_RFM69_ENCRYPTION_KEY must be empty or exactly 16 bytes.");
static_assert(
    sizeof(GATEWAY_RFM69_COMMISSIONING_KEY) == 1 ||
        sizeof(GATEWAY_RFM69_COMMISSIONING_KEY) == 17,
    "GATEWAY_RFM69_COMMISSIONING_KEY must be empty or exactly 16 bytes.");
static_assert(
    GATEWAY_RFM69_SCK != GATEWAY_RFM69_MISO &&
        GATEWAY_RFM69_SCK != GATEWAY_RFM69_MOSI &&
        GATEWAY_RFM69_SCK != GATEWAY_RFM69_CS &&
        GATEWAY_RFM69_SCK != GATEWAY_RFM69_IRQ &&
        GATEWAY_RFM69_MISO != GATEWAY_RFM69_MOSI &&
        GATEWAY_RFM69_MISO != GATEWAY_RFM69_CS &&
        GATEWAY_RFM69_MISO != GATEWAY_RFM69_IRQ &&
        GATEWAY_RFM69_MOSI != GATEWAY_RFM69_CS &&
        GATEWAY_RFM69_MOSI != GATEWAY_RFM69_IRQ &&
        GATEWAY_RFM69_CS != GATEWAY_RFM69_IRQ,
    "RFM69 SPI and interrupt pins must be distinct.");
#if defined(GATEWAY_BOARD_WAVESHARE_S3_ETH)
static_assert(
    (GATEWAY_RFM69_SCK < 9 || GATEWAY_RFM69_SCK > 14) &&
        (GATEWAY_RFM69_MISO < 9 || GATEWAY_RFM69_MISO > 14) &&
        (GATEWAY_RFM69_MOSI < 9 || GATEWAY_RFM69_MOSI > 14) &&
        (GATEWAY_RFM69_CS < 9 || GATEWAY_RFM69_CS > 14) &&
        (GATEWAY_RFM69_IRQ < 9 || GATEWAY_RFM69_IRQ > 14),
    "Waveshare RFM69 pins must not collide with W5500 GPIO9..14.");
static_assert(
    (GATEWAY_RFM69_SCK < 33 || GATEWAY_RFM69_SCK > 37) &&
        (GATEWAY_RFM69_MISO < 33 || GATEWAY_RFM69_MISO > 37) &&
        (GATEWAY_RFM69_MOSI < 33 || GATEWAY_RFM69_MOSI > 37) &&
        (GATEWAY_RFM69_CS < 33 || GATEWAY_RFM69_CS > 37) &&
        (GATEWAY_RFM69_IRQ < 33 || GATEWAY_RFM69_IRQ > 37),
    "ESP32-S3R8 GPIO33..37 are occupied by in-package Octal PSRAM.");
#define GATEWAY_RFM69_IS_S3_STRAPPING_PIN(pin) \
    ((pin) == 0 || (pin) == 3 || (pin) == 45 || (pin) == 46)
static_assert(
    !GATEWAY_RFM69_IS_S3_STRAPPING_PIN(GATEWAY_RFM69_SCK) &&
        !GATEWAY_RFM69_IS_S3_STRAPPING_PIN(GATEWAY_RFM69_MISO) &&
        !GATEWAY_RFM69_IS_S3_STRAPPING_PIN(GATEWAY_RFM69_MOSI) &&
        !GATEWAY_RFM69_IS_S3_STRAPPING_PIN(GATEWAY_RFM69_CS) &&
        !GATEWAY_RFM69_IS_S3_STRAPPING_PIN(GATEWAY_RFM69_IRQ),
    "Waveshare RFM69 pins must not use ESP32-S3 strapping GPIOs.");
#undef GATEWAY_RFM69_IS_S3_STRAPPING_PIN
#endif
static_assert(
    (GATEWAY_RFM69_HIGH_POWER && GATEWAY_RFM69_TX_POWER_DBM >= -2 &&
     GATEWAY_RFM69_TX_POWER_DBM <= 20) ||
        (!GATEWAY_RFM69_HIGH_POWER && GATEWAY_RFM69_TX_POWER_DBM >= -18 &&
         GATEWAY_RFM69_TX_POWER_DBM <= 13),
    "Configured RFM69 TX power is outside the selected module variant's range.");

namespace gateway::radio::config {

constexpr int spiHost = GATEWAY_RFM69_SPI_HOST;
constexpr int sck = GATEWAY_RFM69_SCK;
constexpr int miso = GATEWAY_RFM69_MISO;
constexpr int mosi = GATEWAY_RFM69_MOSI;
constexpr int chipSelect = GATEWAY_RFM69_CS;
constexpr int interrupt = GATEWAY_RFM69_IRQ;
constexpr uint8_t frequencyBand = GATEWAY_RFM69_FREQUENCY;
constexpr uint16_t nodeId = GATEWAY_RFM69_NODE_ID;
constexpr uint8_t networkId = GATEWAY_RFM69_NETWORK_ID;
constexpr bool highPower = GATEWAY_RFM69_HIGH_POWER != 0;
constexpr int8_t txPowerDbm = GATEWAY_RFM69_TX_POWER_DBM;
constexpr char encryptionKey[] = GATEWAY_RFM69_ENCRYPTION_KEY;
constexpr bool encryptionEnabled = sizeof(encryptionKey) == 17;
constexpr char commissioningKey[] = GATEWAY_RFM69_COMMISSIONING_KEY;
constexpr bool commissioningEnabled = sizeof(commissioningKey) == 17;
constexpr uint8_t commissioningNetworkId = GATEWAY_RFM69_COMMISSIONING_NETWORK_ID;

}  // namespace gateway::radio::config
