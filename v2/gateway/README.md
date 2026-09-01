# RadioSensors gateway v2

This directory contains the shared gateway firmware for two supported boards:

- Wireless-Tag WT32-ETH01 (ESP32 + LAN8720/RMII)
- Waveshare ESP32-S3-ETH with the optional PoE module (ESP32-S3 + W5500/SPI)

The boards share one source tree but produce separate firmware binaries. The
firmware must remain functional without allocating application data in PSRAM.

## Toolchain

- PlatformIO Core 6.1 or newer
- PlatformIO Espressif 32 platform 6.12.0 (pinned in `platformio.ini`)
- Arduino framework supplied by that platform

Dependencies are added with exact versions only when first used. This keeps the
iteration-0 build minimal and prevents unused libraries from becoming part of
the firmware baseline.

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

Ethernet and radio initialization intentionally begin in later iterations.

