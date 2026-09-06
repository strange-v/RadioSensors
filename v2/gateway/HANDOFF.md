# Gateway firmware handoff

Last updated: 2026-09-05

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
- Current firmware version: 0.7.0.
- The version lives in `include/FirmwareVersion.h`, not global PlatformIO build
  flags, so changing it invalidates only translation units that include it.
- Shared native tests do not require Windows `gcc/g++` in `PATH`. From the
  repository root run `wsl bash v2/protocol/scripts/run_native_tests_wsl.sh`;
  see `../../ARCHITECTURE_HANDOFF.md` for dependency/bootstrap details.

## Hardware profiles

WT32-ETH01 V1.4 uses the pinout preserved from v1:

- LAN8720 PHY address 1; power GPIO16; MDC GPIO23; MDIO GPIO18; clock GPIO0 input.
- Serial upload speed is 115200. The tested USB-to-UART adapter was unreliable at
  460800.

Waveshare W5500 pins from the official schematic:

- SCK GPIO13; MISO GPIO12; MOSI GPIO11; CS GPIO14; reset GPIO9; interrupt GPIO10.

RFM69 settings are defined entirely in each PlatformIO environment's
`build_flags`:

- WT32-ETH01: HSPI; SCK GPIO12; MISO GPIO15; MOSI GPIO4; CS GPIO14; DIO0 GPIO36.
- Waveshare: separate HSPI bus; SCK GPIO43; MISO GPIO44; MOSI GPIO1; CS GPIO2;
  DIO0 GPIO38. Add a hardware pull-up from CS/NSS to 3.3 V so the module remains
  deselected during reset and optional UART0 recovery on GPIO43/44.
- Never use GPIO33-37 on this ESP32-S3R8 board: they are occupied by in-package
  Octal PSRAM even though they appear on the header/schematic.
- Both profiles currently select RFM69HW, 868 MHz, gateway node 100, network 128,
  and +13 dBm configured TX power.

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

### Iteration 2: RFM69 software bring-up

- LowPowerLab RFM69 1.6.0 is pinned, continuing the v1 driver and packet format.
- GPIO, SPI host, frequency, node/network IDs, module variant, and TX power are
  mandatory compile-time `build_flags`; there are no driver fallback pins.
- Initialization explicitly starts the selected SPI controller, performs the
  library's register write/read probe, and validates `RegVersion == 0x24`.
- Missing or invalid radio hardware fails safely while Ethernet, `/health`, and
  OTA continue running.
- DIO0 follows the v1 interrupt design: the library ISR invokes a minimal
  callback which wakes a dedicated pinned task; all SPI/FIFO work happens in
  task context.
- The receive task runs at FreeRTOS priority 11, above AsyncTCP priority 10,
  because draining the radio FIFO is the gateway's highest-priority application
  work.
- AES reception requires an exactly 16-byte `GATEWAY_RFM69_ENCRYPTION_KEY` in
  ignored `include/LocalSecrets.h`. An empty/missing key leaves the probed radio
  in `encryption_key_missing` with RX disabled. The key is never logged or
  exposed through `/health`.
- The receive-only smoke path uses the library's static 66-byte buffer, records
  bounded diagnostics, and feeds the v2 receive paths.
- `/health` includes radio state/profile/pins, counters, and last-packet data.
- Both firmware environments compile.
- Waveshare radio bring-up is hardware-validated: version `0x24`, encrypted v1
  packets received through DIO0, node 2 observed with RSSI -67 dBm, and Ethernet
  plus OTA remained online.
- Waveshare is the only active hardware test target for current v2 development.
  Keep WT32 compile-supported, but do not block iterations on WT32 hardware
  validation; it may be tested later.

## Completed v2 protocol and commissioning work

The portable common v2 frame-header codec and its native tests now live under
`../shared/RadioProtocol` and `../protocol`. The canonical byte/bit diagrams are
in `../protocol/PROTOCOL.md`. Commissioning payloads are frozen; command/result,
error payloads, and individual telemetry profiles remain intentionally unfrozen.

The `JOIN_REQUEST` codec is frozen and tested as a fixed 20-byte frame: header,
10-byte tinyAVR factory UID, 16-bit profile ID, three firmware-version bytes,
and a 32-bit request nonce. Profile ID replaces separate node-type, telemetry-
schema, and capability fields and describes the node's complete contract.

The persistent `NodeRegistry` is also implemented and loaded at boot. It holds
at most 64 fixed records, allocates node IDs 1..99, and persists versioned CRC32
snapshots alternately in NVS keys `registry_a` and `registry_b`. Pending records
persist the latest request nonce; activation requires matching UID, allocated
node ID, and nonce. `/health` exposes only record count and generation. See
`../protocol/REGISTRY.md` for the binary layout and state rules.

Waveshare local pairing control is implemented for firmware 0.5.0. A debounced
runtime press of BOOT/GPIO0 toggles a 120-second window; a button already held at
startup is ignored so recovery flashing does not open pairing. The onboard
WS2812 on GPIO21 is solid green in operational mode and breathes blue while the
window is open. Yellow/persisting, cyan/awaiting-confirm, white/success,
red/error, and magenta/profile-conflict indications are driven by the
commissioning task. Pairing status is included in `/health`.

Radio ownership is now centralized in the priority-11 radio task. A queue-set
wakes it for either a coalesced ISR signal or an 8-entry command queue, always
servicing pending RX first. Valid v2 frames enter a non-blocking 16-entry RX
queue; profile changes and sends enter the command queue. Queue acceptance,
drops, and processed commands are visible in `/health`. BOOT pairing commands
now switch between operational network/key and commissioning network 0 plus the
separate `GATEWAY_RFM69_COMMISSIONING_KEY`. An empty commissioning key makes the
button show an error instead of opening an ineffective window.

`JOIN_ACCEPT`, `JOIN_CONFIRM`, and `JOIN_COMPLETE` codecs are frozen and covered by native
tests. Join accept is 34 bytes and delivers the assigned node ID, gateway ID,
operational network ID, and 16-byte installation key while echoing UID and
nonce. Join confirm is 15 bytes and echoes UID and nonce from the newly assigned
transport address. Join complete is the gateway's 15-byte durable-activation
acknowledgement. `../protocol/PROTOCOL.md` contains the exact byte maps and
documents the security limits of the shared commissioning-key model.

The priority-6 commissioning consumer drains the RX queue, decodes Join request
and confirm, transactionally persists registry transitions, and queues radio
commands. Registry access is mutex-protected and NVS never runs in the radio
task. A successful activation closes the pairing window, so one BOOT press adds
one node. Duplicate matching Join confirm frames receive another Join complete
without rewriting NVS.

Active-registry-validated telemetry reception is implemented in firmware
0.6.0. The registry publishes a lock-free active-node bitmap after loading or a
successful durable commit. The priority-11 radio task ACKs telemetry only when
the sender is active and the frame entered the bounded telemetry queue. Unknown,
pending, disabled, and queue-full frames are not acknowledged. The main loop
currently drains the queue and logs sender ID, size, and RSSI without decoding
the opaque payload. `/health` exposes ACK, rejection, queue, and drop counters.

The telemetry pipeline and initial WebSocket slice are hardware-validated on
Waveshare with the climate node. A fixed cache retains the latest complete v2
frame, profile ID, RSSI, receive time, and sequence for every node that has
reported since boot.
`GET /telemetry/last` exposes the latest record for diagnostics, and `/health`
reports cache plus WebSocket counters. `ws://<gateway>/ws` sends a versioned
binary snapshot on connect and then live telemetry. Slow clients are
disconnected on bounded-queue overflow and must reconnect for resynchronization.
The exact envelope is documented in `WEBSOCKET.md` and covered by a portable
known-vector test. OTA validation of firmware 0.7.0 confirmed a three-message
snapshot followed by a live telemetry push; `/health` reported three accepted
telemetry updates, three radio ACKs, two client connections, and zero WebSocket
drops.

Firmware 0.7.0 uses SNTP after Ethernet obtains an address. Cached telemetry and
the WebSocket envelope carry a single 64-bit UTC Unix-millisecond timestamp;
zero means the packet arrived before time synchronization. Gateway uptime is
not part of the stream. `/health` reports the time state, current Unix time, and
last successful synchronization time.

If RFM69 initialization fails, the commissioning task is not started. Queue
receive APIs also honor a nonzero wait even before queue creation, preventing a
failed-radio path from turning the priority-6 commissioning task into a tight
loop that starves `loopTask` and triggers the task watchdog.

RFM69 startup now makes three register-probe attempts, returning CS high and
restarting the dedicated SPI host between attempts. Pairing profile changes and
the post-`JOIN_ACCEPT` switch are synchronously confirmed by the radio-owner
task; UI/pairing state is not reported as successfully changed after a mere
queue insertion. Manual close, expiry, confirm timeout, and successful pairing
surface a radio-switch failure instead of silently diverging from radio state.

Remaining Waveshare reliability checks:

- Exercise sustained receive traffic and confirm no FIFO loss or watchdog reset.
- Validate Ethernet link recovery and OTA while radio traffic is active.
- WT32 radio hardware validation is optional and deferred.

Next milestones, to refine against `ARCHITECTURE_HANDOFF.md`:

1. Complete ATtiny1614 commissioning and telemetry hardware validation.
2. Idempotent queued commands and acknowledgements.
3. Add Home Assistant mDNS discovery and the HA WebSocket client, including
   reconnect/resync behavior.
4. Embedded management UI plus configuration export/restore.
5. Security, recovery, and long-duration reliability testing.

For milestone 2, publish a lock-free per-node pending-command bitmap only after
the command is durably stored. When acknowledging accepted telemetry, use an
empty ACK if no command is pending or a one-byte versioned `COMMAND_PENDING`
frame-kind payload when one is pending. The node then initiates the existing
`COMMAND_READY` pull exchange; the full command is deliberately not embedded in
the ACK. Clear the pending bit only after the matching durable command result is
processed.

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
- `src/RadioService.cpp`: sole RFM69 owner and bounded radio queues.
- `src/CommissioningService.cpp`: gateway join state machine.
- `src/NodeRegistryStore.cpp`: mutex-protected transactional NVS registry.
- `../shared/RadioProtocol`: shared portable codecs and registry core.
- `../protocol/PROTOCOL.md`: canonical v2 wire format.
- `../protocol/REGISTRY.md`: canonical registry/storage format.
- `scripts/ota_upload.py`: authenticated OTA upload flags.
- `README.md`: build and usage instructions.
