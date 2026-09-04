# RadioSensors gateway v2

Current implementation status and next-agent notes are maintained in
[`HANDOFF.md`](HANDOFF.md).

This directory contains the shared gateway firmware for two supported boards:

- Wireless-Tag WT32-ETH01 (ESP32 + LAN8720/RMII)
- Waveshare ESP32-S3-ETH with the optional PoE module (ESP32-S3 + W5500/SPI)

The boards share one source tree but produce separate firmware binaries. The
firmware must remain functional without allocating application data in PSRAM.

## Toolchain

- PlatformIO Core 6.1 or newer
- pioarduino Espressif 32 platform 55.03.311 (pinned release URL)
- Arduino-ESP32 3.3.11, based on ESP-IDF 5.5.5
- ESP32Async/AsyncTCP 3.5.0
- ESP32Async/ESPAsyncWebServer 3.12.0

Dependencies are added with exact versions when first used.

## Build

Build both supported targets:

```powershell
pio run
```

Build one target:

```powershell
pio run -e gateway_wt32_eth01
pio run -e gateway_waveshare_s3_eth
```

If PlatformIO is installed in its default Windows location but is not in `PATH`:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

## Iteration 0 acceptance criteria

- Both environments compile from the same source tree.
- Each build rejects an incompatible MCU at compile time.
- The serial startup banner identifies the firmware, board, Ethernet controller,
  and whether the selected hardware profile includes PoE.
- No application code depends on PSRAM.

## Iteration 1

Iteration 1 adds event-driven Ethernet startup and a diagnostic HTTP endpoint:

```text
GET http://<gateway-ip>/health
```

WT32-ETH01 uses the LAN8720 pinout preserved from the v1 firmware. Waveshare
ESP32-S3-ETH uses the onboard W5500 pinout from the official schematic: SCK 13,
MISO 12, MOSI 11, CS 14, reset 9, and interrupt 10.

The health response contains firmware and board identity, reset reason, uptime,
free internal heap, and current Ethernet state. It intentionally does not use
PSRAM.

## Iteration 1.5: development OTA

OTA uses ArduinoOTA over the active Ethernet interface. Copy
`include/LocalSecrets.example.h` to the Git-ignored `include/LocalSecrets.h` and
replace the sample password. Without a non-empty password the OTA service stays
disabled.

After Ethernet receives an address, the serial log and `/health` response show
the unique hostname and OTA state. The hostname is derived from the factory MAC,
for example `rf-gateway-a0a3b3230e84.local`.

The validated WT32-ETH01 can be uploaded over Ethernet with:

```powershell
pio run -e gateway_wt32_eth01_ota -t upload
```

The validated Waveshare ESP32-S3-ETH can be uploaded over Ethernet with:

```powershell
pio run -e gateway_waveshare_s3_eth_ota -t upload
```

The OTA environments read the password from `include/LocalSecrets.h`; it is not
stored in `platformio.ini`. They use local TCP port 3233 to avoid Windows dynamic
port exclusions. Keep using the non-OTA environments for serial or USB recovery.
