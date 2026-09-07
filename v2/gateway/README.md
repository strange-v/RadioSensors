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

Copy `include/LocalSecrets.example.h` to the Git-ignored `include/LocalSecrets.h`. A nonempty OTA password enables ArduinoOTA. Operational and per-device commissioning radio keys are generated or entered at runtime and are never taken from this build-time header. Existing invalid secret data is never overwritten automatically.

OTA targets:

```powershell
pio run -e gateway_wt32_eth01_ota -t upload
pio run -e gateway_waveshare_s3_eth_ota -t upload
```

Use a non-OTA target for serial or USB recovery.

## Web UI filesystem

The separately versioned Web UI is stored in a LittleFS partition named `web`. Its partition-table subtype remains the legacy `spiffs` value required by the pinned ArduinoOTA/PlatformIO filesystem command; the bytes, generator, and mounted filesystem are LittleFS. The source frontend remains outside the firmware source tree; its production output is copied to `data/` before building the filesystem image. The gateway never formats LittleFS automatically. A missing, damaged, or incompatible image therefore leaves the REST API operational and serves a small recovery page from firmware instead of erasing evidence or configuration.

Every UI image must contain `/index.html` and `/ui-manifest.json`:

```json
{"ui_version":"0.1.0","api_version":1,"build":"git-sha"}
```

`ui_version` identifies independently released UI fixes. `api_version` is the gateway client-contract version and changes only for an incompatible REST/WebSocket contract change. `build` is diagnostic metadata and is not used for compatibility. The firmware serves the UI only when the manifest API version exactly equals its own API version.

The Vue 3 frontend source lives in `ui/`. The generated `data/` directory is
git-ignored and must not be committed. From `ui/`:

```powershell
npm ci
npm run dev
npm test
npm run build
```

The development server proxies API requests to `http://rf-gateway.local`. Set
`GATEWAY_URL` before `npm run dev` to use another hostname. The production build
replaces the generated contents of `data/`, writes the manifest from the package
version, API version, and Git SHA, precompresses JavaScript and CSS as deterministic
gzip files, and enforces compressed and total-size budgets. `index.html` and
`ui-manifest.json` remain uncompressed because firmware reads them directly.

Run `npm run build` first, then build or upload the generated contents of
`data/` over a local cable:

```powershell
pio run -e gateway_wt32_eth01 -t buildfs
pio run -e gateway_wt32_eth01 -t uploadfs
pio run -e gateway_waveshare_s3_eth -t uploadfs
```

Upload it over the existing authenticated ArduinoOTA connection:

```powershell
pio run -e gateway_wt32_eth01_ota -t uploadfs
pio run -e gateway_waveshare_s3_eth_ota -t uploadfs
```

Firmware and filesystem uploads are deliberately separate. Firmware retains A/B OTA rollback; the LittleFS partition does not. An interrupted filesystem update can make the UI unavailable, but it does not affect the API, configuration, or the embedded recovery page. Retry `uploadfs` to recover it.

Changing a partition table is not part of an application OTA. Existing gateways must therefore receive one cable upload with the new firmware layout before their first LittleFS upload. The NVS location is unchanged, but back up important configuration before repartitioning. Subsequent firmware and UI releases can use OTA normally while the layout remains unchanged.

## Runtime

One priority-11 task owns RFM69 and all FIFO/SPI operations. Active telemetry enters bounded queues and is acknowledged only after acceptance. Commissioning and NVS writes execute outside the radio task. The main loop keeps the latest opaque telemetry frame for each node and publishes it through the binary WebSocket.

The gateway exposes `/health`, `/api/v1/info`, the setup REST endpoints, `/telemetry/last` for development diagnostics, and `/ws`. Persistent settings, authentication, registry, and secrets use independent dual-slot stores. SNTP uses configured NTP servers and reapplies changes without reboot.

Before the first user exists, the Waveshare status LED blinks green and a short BOOT press opens the physical setup window. `POST /api/v1/setup` creates the first admin and can set the display name and operational network ID. Pairing is opened from the management UI after manually entering the node UID and its unique factory key; a short BOOT press can close an active pairing window.

References:

- [REST API](API.md)
- [WebSocket binary protocol](WEBSOCKET.md)
- [Persistent storage](STORAGE.md)
- [System architecture](../../ARCHITECTURE.md)
- [Unfinished work](../../ROADMAP.md)
