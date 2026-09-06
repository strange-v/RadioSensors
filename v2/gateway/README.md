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
- LowPowerLab RFM69 1.6.0

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

## Iteration 2: RFM69 bring-up

The radio continues to use the LowPowerLab RFM69 driver and on-air defaults from
the v1 gateway. Wiring, SPI host, and module variant are mandatory compile-time
PlatformIO `build_flags`; the driver contains no fallback GPIO values.

WT32-ETH01 preserves the v1 wiring: SCK 12, MISO 15, MOSI 4, CS 14, and DIO0/IRQ
36. Waveshare uses `HSPI`, independently of the W5500 `FSPI` bus: SCK 43, MISO
44, MOSI 1, CS 2, and DIO0/IRQ 38. GPIO33-37 are unavailable because the
ESP32-S3R8 uses them for its in-package Octal PSRAM. Add a hardware pull-up from
RFM69 CS/NSS to 3.3 V so the module stays deselected during reset and UART0
recovery on GPIO43/44.

The firmware validates the register interface and `RegVersion == 0x24`, then
starts the receive service. The library ISR wakes a dedicated FreeRTOS
task pinned to core 1 at priority 11, above AsyncTCP priority 10. All SPI/FIFO
work remains outside the ISR and uses the library's bounded 66-byte static
buffer. Active-node telemetry is acknowledged and passed through a bounded
opaque queue. AES reception is enabled only when
`GATEWAY_RFM69_ENCRYPTION_KEY` in the Git-ignored `LocalSecrets.h` contains
exactly 16 bytes. With a missing key the radio hardware is still probed, but RX
remains disabled with state `encryption_key_missing`.

An absent or invalid radio does not stop Ethernet, `/health`, or OTA. The health
response reports the compiled radio profile and pins, state, detected version,
frequency/bit rate, whether encryption is enabled, counters, and last-packet
metadata. It never exposes the encryption key.

Current v2 development and hardware testing target the Waveshare board only.
WT32 remains compile-supported, but its RFM69 path is not currently part of the
iteration acceptance gate and may be validated later.

## Iteration 3: common v2 frame header

The portable zero-allocation codec is shared from `../shared/RadioProtocol`.
Its byte/bit specification and native tests live in `../protocol`. The receive
task validates the common header and exposes v2/telemetry/rejection counters in
`/health`, while leaving telemetry payload bytes opaque. Registry-validated
telemetry forwarding remains deferred.

## Iteration 4: persistent node registry

The gateway loads a fixed-capacity node registry from NVS during startup. Its
CRC-protected versioned snapshots alternate between two NVS slots so an
interrupted write leaves the previous generation recoverable. Persistent
records contain UID, assigned node ID, profile ID, firmware version, lifecycle
state, and the pending request nonce; high-churn telemetry diagnostics remain
RAM-only. `/health` reports the record count and storage generation. A lock-free
active-node bitmap lets the radio task validate telemetry in time for the
RFM69 ACK window without taking the registry mutex or touching NVS.

The commissioning task uses mutex-protected transactional reserve/confirm APIs;
NVS is never accessed by the radio-owner task. See
`../protocol/REGISTRY.md` for the canonical layout and allocation rules.

## Iteration 5: local pairing control and status LED

On Waveshare, pressing the runtime BOOT button on GPIO0 toggles a 120-second
pairing window. Holding BOOT during reset retains its normal ROM download-mode
purpose and does not automatically open pairing after firmware startup. The
onboard WS2812 on GPIO21 indicates gateway/commissioning state:

| Indication | Meaning |
| --- | --- |
| Solid green | Normal operational mode |
| Breathing blue | Pairing window open |
| Pulsing yellow | Join request accepted; registry commit in progress |
| Fast cyan blink | Join accept sent; waiting for Join confirm |
| Short white flash | Node activated successfully |
| Fast red blink | Storage or protocol failure |
| Double magenta blink | Existing UID/profile conflict |

Opening/closing the window queues a radio-profile
switch; commissioning is refused with a red error indication when the separate
16-byte `GATEWAY_RFM69_COMMISSIONING_KEY` is missing. `/health`
reports pairing activity, remaining seconds, and the current indication. WT32
has no pairing-button or RGB implementation in the current hardware-test scope.

## Iteration 6: single radio owner and bounded queues

Only the priority-11 radio task accesses the RFM69 object or its SPI bus. The ISR
writes a coalesced one-byte signal to a queue; the task drains the FIFO before
processing competing commands. Valid v2 frames are copied into a 16-entry,
fixed-size RX queue. Profile switches and transmissions use a separate 8-entry
command queue. Producers never block: full queues reject the new item and expose
drop counters through `/health`.

The commissioning profile uses network ID 0 and the separate shared factory key
from `LocalSecrets.h`. The operational installation key is never reused as the
commissioning key. A priority-6 consumer drains the RX queue and performs all
registry persistence outside the radio task. After a durable reservation it
queues Join accept and switches to the operational profile. After a matching
Join confirm is durably activated, it queues Join complete. A matching repeated
confirm resends Join complete without another NVS write.

One BOOT pairing window provisions one node. Successful activation closes the
window and leaves the radio operational; press BOOT again to add another node.

## Iteration 7: active-node telemetry acknowledgement

The radio task validates telemetry sender IDs against a lock-free snapshot of
active registry entries. It sends an RFM69 ACK only after valid telemetry has
entered the bounded telemetry queue; unknown, pending, disabled, or queue-full
traffic is not acknowledged. The main loop currently drains this queue and logs
only sender ID, payload size, and RSSI while keeping the payload opaque. The
future WebSocket service will replace this diagnostic consumer.
