# RadioSensors gateway v2

Shared ESP32 firmware for:

- Wireless-Tag WT32-ETH01 with LAN8720/RMII Ethernet.
- Waveshare ESP32-S3-ETH with W5500/SPI Ethernet and optional PoE.

The application does not require PSRAM. Current firmware version is declared in `include/FirmwareVersion.h`.

## Build

From this directory:

```powershell
pio run
pio run -e gateway_wt32_eth01
pio run -e gateway_waveshare_s3_eth
```

PlatformIO is pinned in `platformio.ini`, along with exact library versions. On the current Windows workstation the executable is available at `C:\Users\stran\.platformio\penv\Scripts\platformio.exe` when it is not in `PATH`.

## Hardware

WT32 RFM69 wiring: SCK 12, MISO 15, MOSI 4, CS 14, DIO0/IRQ 36. Waveshare RFM69 uses a separate HSPI bus from W5500: SCK 43, MISO 44, MOSI 1, CS 2, DIO0/IRQ 38. Add a pull-up from RFM69 CS to 3.3 V so it remains deselected through reset and UART recovery.

Waveshare BOOT/GPIO0 is the runtime physical-presence button and GPIO21 drives the onboard RGB LED. WT32 compiles from the same source but currently has no equivalent runtime button/LED workflow and is outside the primary hardware acceptance scope.

## Local secrets and OTA

Copy `include/LocalSecrets.example.h` to the Git-ignored `include/LocalSecrets.h`. A nonempty OTA password enables ArduinoOTA. Valid 16-byte development radio keys are imported into an empty durable secrets store once; after that NVS is the runtime source of truth. Existing invalid secret data is never overwritten automatically.

OTA targets:

```powershell
pio run -e gateway_wt32_eth01_ota -t upload
pio run -e gateway_waveshare_s3_eth_ota -t upload
```

Use a non-OTA target for serial or USB recovery.

## Runtime

One priority-11 task owns RFM69 and all FIFO/SPI operations. Active telemetry enters bounded queues and is acknowledged only after acceptance. Commissioning and NVS writes execute outside the radio task. The main loop keeps the latest opaque telemetry frame for each node and publishes it through the binary WebSocket.

The gateway exposes `/health`, the setup REST endpoints, `/telemetry/last` for development diagnostics, and `/ws`. Persistent settings, authentication, registry, and secrets use independent dual-slot stores. SNTP uses configured NTP servers and reapplies changes without reboot.

Before the first user exists, a short BOOT press opens the physical setup window. `POST /api/v1/setup` creates the first admin and can set the display name and operational network ID. After setup, the same short press controls node pairing.

References:

- [REST API](API.md)
- [WebSocket binary protocol](WEBSOCKET.md)
- [Persistent storage](STORAGE.md)
- [System architecture](../../ARCHITECTURE.md)
- [Unfinished work](../../ROADMAP.md)
