# Gateway firmware handoff

Last updated: 2026-09-04

This is the operational handoff for the gateway firmware. Read
`../../ARCHITECTURE_HANDOFF.md` first for system-level protocol and Home Assistant
decisions, then use this file for current implementation status.

## Scope and supported hardware

One source tree supports:

- Wireless-Tag WT32-ETH01 V1.4: ESP32 with LAN8720 RMII Ethernet.
- Waveshare ESP32-S3-ETH with optional PoE: ESP32-S3R8 with W5500 SPI Ethernet.

PSRAM is not required. Application code must remain functional without allocating
application data in it.

## Toolchain and workspace

- PlatformIO root: `v2\gateway`.
- On the current Windows setup, always open and build the checkout through its
  native `D:\...` path, not through the `C:\D\...` alias. Absolute paths affect
  SCons signatures and switching between them causes unnecessary full rebuilds.
- pioarduino 55.03.311 (immutable release URL).
- Arduino-ESP32 3.3.11 on ESP-IDF 5.5.5.
- ESP32Async/AsyncTCP 3.5.0 and ESPAsyncWebServer 3.12.0.
- Current firmware version: 0.1.5.

## Hardware profiles

WT32-ETH01 V1.4 uses the pinout preserved from v1:

- LAN8720 PHY address 1; power GPIO16; MDC GPIO23; MDIO GPIO18; clock GPIO0 input.
- Serial upload speed is 115200. The tested USB-to-UART adapter was unreliable at
  460800.

Waveshare W5500 pins from the official schematic:

- SCK GPIO13; MISO GPIO12; MOSI GPIO11; CS GPIO14; reset GPIO9; interrupt GPIO10.

## Completed work

### Iteration 0: foundation

- Shared sources with separate compile-time board profiles.
- Startup diagnostics identify firmware, board, Ethernet, PoE profile, reset
  reason, hostname, and watchdog state.
- Both targets compile.

### Iteration 1: Ethernet and health

- Event-driven LAN8720 and W5500 startup with link/address tracking.
- Async `GET /health` endpoint reports identity, reset reason, uptime, internal
  heap, Ethernet state/IP/MAC, and OTA state.
- Arduino loop watchdog is enabled through `enableLoopWDT()`.
- WT32 hardware validation confirmed health reporting and link recovery.
- Waveshare hardware validation confirmed health reporting and link operation.

Already-fixed boot faults:

- Do not initialize/reconfigure TWDT directly; Arduino already owns it.
- Start Ethernet before `AsyncWebServer::begin()`. Reversing this caused a null
  FreeRTOS queue assertion before lwIP was ready.

### Iteration 1.5: development OTA

- ArduinoOTA runs over Ethernet on device port 3232.
- Hostnames are `rf-gateway-<factory-base-mac>`.
- OTA is disabled if ignored `include/LocalSecrets.h` is absent or has an empty
  password. Start from `LocalSecrets.example.h`.
- `scripts/ota_upload.py` reads the password locally; it is not stored in
  `platformio.ini`.
- The uploader binds local TCP port 3233. Random ports can fall into Windows
  excluded ranges and fail with WinError 10013.
- Serial/USB environments remain available for recovery.

| Environment | Hostname | Validation status |
| --- | --- | --- |
| `gateway_wt32_eth01_ota` | `rf-gateway-a0a3b3230e84.local` | Build and hardware OTA upload confirmed |
| `gateway_waveshare_s3_eth_ota` | `rf-gateway-a085e3e6cc20.local` | Build and hardware OTA upload confirmed |

Upload commands:

```powershell
pio run -e gateway_wt32_eth01_ota -t upload
pio run -e gateway_waveshare_s3_eth_ota -t upload
```

If mDNS fails, append `--upload-port <device-ip>` using the device's current DHCP
address. DHCP leases are not part of the permanent configuration.

## Next work

The next milestone is Iteration 2: RFM69 bring-up. Keep it independently testable
before provisioning or Home Assistant transport.

Proposed acceptance criteria:

- Define collision-free RFM69 pins for both boards.
- Add and pin a maintained RFM69 library compatible with the current Arduino core.
- Initialize the SPI bus and radio explicitly; fail safely if the module is absent.
- Read and validate the RFM69 version/register interface.
- Add radio presence, initialization state, frequency/profile, and counters to
  serial diagnostics and `/health`.
- Add a receive-only, bounded, watchdog-safe smoke test.
- Build and physically validate both boards.

First confirm physical RFM69 wiring. Waveshare already uses SPI for W5500; decide
whether RFM69 shares that bus with a separate CS or uses another SPI peripheral
and pin set.

Later milestones, to refine against `ARCHITECTURE_HANDOFF.md`:

1. Common radio frame codec and opaque telemetry receive path.
2. Persistent node registry and installation radio configuration.
3. Explicit, time-bounded pairing and commissioning.
4. Idempotent queued commands and acknowledgements.
5. Gateway WebSocket API, bounded queues, reconnect/resync, and Home Assistant
   mDNS discovery.
6. Embedded management UI plus configuration export/restore.
7. Security, recovery, and long-duration reliability testing.

## Constraints to preserve

- Gateway owns the registry, commands, pairing, and installation radio settings.
- Telemetry is opaque to the gateway; schemas belong to consumers such as HA.
- Do not reintroduce MQTT; HA uses a gateway-hosted WebSocket.
- Never expose the radio AES key through the normal web API or HA.
- Retry the exact command ID/payload and allow at most one outstanding
  state-changing command per node.
- Pairing is explicitly user-enabled and time-bounded.

## Relevant files

- `platformio.ini`: build and upload environments.
- `include/BoardProfile.h`: board Ethernet profiles.
- `src/EthernetService.cpp`: Ethernet lifecycle.
- `src/HealthServer.cpp`: diagnostics endpoint.
- `src/DeviceIdentity.cpp`: stable hostname.
- `src/OtaService.cpp`: OTA lifecycle.
- `scripts/ota_upload.py`: authenticated OTA upload flags.
- `README.md`: build and usage instructions.
