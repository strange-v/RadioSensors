# RadioSensors Architecture Handoff

Gateway firmware implementation progress is tracked separately in
[`v2/gateway/HANDOFF.md`](v2/gateway/HANDOFF.md).

## Source-of-truth documents and shared code

- `v2/protocol/PROTOCOL.md` is the canonical byte-level v2 wire specification.
- `v2/protocol/REGISTRY.md` is the canonical gateway registry and persistence specification.
- `v2/shared/RadioProtocol` contains portable, zero-allocation codecs shared by
  the gateway and the active `v2/node` implementation, plus the gateway
  registry core.
- `v2/protocol/test` contains native Unity tests for the shared components.
- `v2/gateway/HANDOFF.md` records gateway implementation and hardware status.
- `v2/node/HANDOFF.md` records node implementation status, verified sizes, and
  the exact continuation sequence.

Do not duplicate wire layouts as packed C/C++ structs in gateway or node code.
Change the protocol document, shared codec, and its known-vector test together.

### Running shared native tests on Windows

The Windows host currently has no `gcc/g++` in `PATH`, so PlatformIO's native
test runner cannot compile the suites directly. From the repository root, use
the checked-in WSL runner instead:

```powershell
wsl bash v2/protocol/scripts/run_native_tests_wsl.sh
```

The runner uses WSL `g++`, relative repository paths, and PlatformIO's downloaded
Unity sources. If Unity has not been resolved yet, run `platformio test -e
native` once from `v2/protocol`; its compile step may fail on Windows after it
downloads the test dependency. Then rerun the WSL command. Do not hard-code a
user profile, PlatformIO installation path, or checkout drive in documentation
or automation.

## Project context

- Existing nodes use ATmega328P; all new node designs will use ATtiny1614.
- Some node types must retain state, especially pulse counters for gas/water meters.
- Counter persistence currently uses EEWL wear levelling. Preserve this approach and re-check exactly when `node_counter` writes after the firmware changes.
- New outdoor nodes may be conformal-coated, solar powered, and connected to a supercapacitor, so physical access and continuous RX are not assumed.

## Node identity and provisioning

- Ship every new node with operational `NODE_ID = 0` (unprovisioned state).
- New node boards have a physical button. A short press opens an explicit receive/configuration window when supported by that node type. Holding it for more than 10 seconds performs a factory reset/reprovisioning reset. Counter state should be treated separately from network configuration so an accidental factory reset does not silently erase the measured total unless explicitly designed otherwise.
- Use the ATtiny1614 factory 10-byte UID during provisioning. Keep the compact node ID in normal radio packets.
- An unprovisioned node periodically sends a fixed 20-byte `JOIN_REQUEST` containing the 10-byte factory UID, a 16-bit profile ID, three firmware-version bytes, and a 32-bit request nonce. The common header carries the protocol-major version.
- Do not listen for five seconds every minute: RFM69 RX current makes this far too expensive. Use a short response window, randomized jitter, and exponential backoff. A button/reed switch may trigger a faster registration cycle where available.
- Gateway pairing mode is explicitly enabled by the user. With one RFM69, the gateway may temporarily switch between the commissioning and operational radio profiles.
- Approval reserves a stable `UID -> NODE_ID` mapping. Repeated requests from the same UID must receive the same reserved ID.
- `JOIN_ACCEPT` is a fixed 34-byte frame containing the UID, echoed nonce, assigned node ID, gateway ID, operational network ID, and 16-byte installation key.
- The node validates UID/nonce, writes the configuration atomically, switches to its assigned ID, and sends `JOIN_CONFIRM` from that ID. The gateway marks it active only after confirmation.
- After durably marking the node active, the gateway sends `JOIN_COMPLETE` with the same UID and nonce. The node remains provisional until it receives this application-level acknowledgement and repeats `JOIN_CONFIRM` if necessary. A duplicate matching confirm only resends `JOIN_COMPLETE`; it does not rewrite the registry.
- Reserve ID `0` for commissioning and reserve gateway/broadcast/service IDs explicitly.

## Radio configuration

- `GATEWAY_ID`: keep a fixed protocol address (currently 100). It is not a security identifier. It may still be stored in the node configuration for future flexibility.
- `NETWORK_ID`: generate per installation. It is an 8-bit sync/filter value, not a security boundary.
- `ENCRYPTION_KEY`: generate a unique 16-byte AES key per installation. All nodes belonging to one gateway can share it.
- `POWER_LEVEL`: provide a firmware default chosen for each node type/power profile during the initial flash. The user may later change it from the gateway UI, and the selected value is persisted in the node configuration. Do not enforce per-node limits for simplicity.
- Store operational radio configuration in node EEPROM with a magic value, schema version, and CRC. Use two slots or another atomic update method. These writes are rare and do not need EEWL.
- The RFM69 library already supports runtime changes through `setAddress()`, `setNetwork()`, `encrypt()`, and `setPowerLevel()`; per-node recompilation is not required.

## Commissioning security

- The installation AES key must be delivered while the node is still using a commissioning profile.
- Minimum acceptable design: a shared factory commissioning key plus a short, explicitly enabled gateway pairing window.
- Stronger optional design: provision each node with a random device secret and print `UID + secret` as a QR code. Use it to authenticate provisioning and securely deliver the installation configuration.
- UID is public identity, not a secret.
- Do not expose the radio encryption key to Home Assistant or the normal gateway web API.
- Reassess before production whether the product threat model requires the per-device secret/QR flow or whether the shared commissioning key remains sufficient.
- The current implementation uses the minimum shared 16-byte factory commissioning key, separate from the installation key, and commissioning network ID 0.

## Counter commands

- Commands use a pull/mailbox model. The gateway persists a UI-created command
  but does not transmit while the node sleeps. A short button press makes the
  node send `COMMAND_READY` with a session nonce and enter a bounded receive
  window. The gateway replies with the one pending command or `NO_COMMAND`.
- Normal telemetry acknowledgement windows never deliver application commands.
- One command is delivered per button session. A missing result leaves the
  exact command pending; the next session retries it and the node's durable
  command ID prevents reapplication.

Planned refinement: use the payload capability of the RFM69 transport ACK to
advertise that the gateway has a durable pending command. An empty ACK means no
hint; a one-byte, versioned `COMMAND_PENDING` frame-kind hint causes the node to
immediately start the existing `COMMAND_READY` pull session with a fresh nonce.
The command itself is not carried in the ACK. The physical button remains a
manual way to open the same session without waiting for telemetry. Gateway
radio code must read the pending state from a lock-free snapshot published only
after durable command storage, never by taking the registry/command mutex in the
ACK timing path. A stale positive hint safely ends in `NO_COMMAND`; a lost hint
is retried with later acknowledged telemetry.

- Gateway UI will support an absolute `SET_COUNT` command so a counter node can be initialized from the real meter value.
- Gateway UI will also support changing a node's `POWER_LEVEL`; the node persists the new value and confirms the applied setting.
- Include a monotonically increasing command sequence/ID. The gateway persists the pending command and retries the exact same command ID and payload when its response is lost.
- The node must persist the accepted command ID atomically with the updated counter state before replying. A duplicate command must not apply `SET_COUNT` again because pulses may have arrived since the first application; it only resends the previous result/current state. Reject commands older than the last accepted sequence.
- Do not add command IDs, revisions, or other provisioning metadata to regular telemetry. Keeping every normal radio transmission as small as possible is a core energy-efficiency requirement. Command IDs exist only in the rare command/response exchange.
- Node response should include command ID, old value, applied value/current value, and success/failure.
- Allow only one outstanding state-changing command per node. Gateway considers the command complete only after a matching command response. If it is lost, the gateway retries the same command later; the node recognizes the duplicate, does not reapply it, and sends the response again. The UI must show an explicit old-to-new confirmation before sending.

## Event-node reporting

- Plain door and counter nodes use a one-hour rolling keep-alive measured from
  their last successfully acknowledged telemetry transmission. An event report
  resets that deadline; a failed transmission does not.
- Plain door nodes report confirmed state changes immediately.
- Counter nodes persist every pulse immediately but aggregate radio reports to
  no more than one per minute while the count is changing. Telemetry
  always carries the absolute cumulative count, so intermediate radio reports
  are unnecessary.
- Door/counter profiles containing TMP112 or SHT40 follow the separately chosen
  climate sampling/reporting interval instead of the plain event-node policy.

## Gateway redesign

- Current hardware acceptance target is Waveshare ESP32-S3-ETH + PoE. WT32 remains compile-supported but is not currently hardware-gated.
- Only the priority-11 radio-owner task may access RFM69 or its SPI bus. DIO0 ISR work is limited to posting a coalesced event. The owner drains RX before commands and feeds a fixed 16-frame RX queue; profile/send requests use a fixed 8-command queue. Producers never block and drops are observable.
- The priority-6 commissioning task owns the join state machine and performs NVS transactions outside the radio task. Registry access is mutex-protected.
- The registry holds at most 64 records, allocates node IDs 1..99, and persists CRC-protected generations in alternating NVS blobs `registry_a` and `registry_b`.
- On Waveshare, a runtime BOOT press opens a 120-second pairing window. One window provisions one node and closes after successful activation. The RGB LED reports operational, pairing, persistence, confirmation, success, conflict, and error states.

- Keep telemetry payloads opaque to the gateway. During registration it stores one numeric profile ID alongside the UID and assigned node ID. The profile is a stable opaque key that defines the telemetry layout and supported commands; it may provide default HA entities but does not encode installation presentation such as gas/water or door/window. The gateway may attach the stored profile ID plus transport metadata such as sender ID and RSSI, but it must not decode sensor fields. Profiles remain owned by the HA integration (and other consumers), where per-node presentation can be remapped.
- Every application radio packet starts with a one-byte frame header: protocol major in bits 7..5 and frame kind in bits 4..0. Defined kinds cover telemetry, join request/accept/confirm/complete, command, command result, and error. This byte replaces the old repeated node-type byte, so normal telemetry does not grow.
- The gateway only interprets this common frame header and the transport/control plane: provisioning frames, generic command envelopes/results, acknowledgements, and radio configuration. For telemetry it strips/uses the frame header and otherwise treats the remaining payload as opaque.
- Gateway becomes the source of truth for:
  - registered and pending nodes;
  - `UID -> NODE_ID` allocation;
  - installation radio configuration;
  - pairing state and retries;
  - queued commands for sleeping nodes;
  - latest node values and command results.
- Add an embedded website for node registration, naming, status, RSSI, last request/last seen, pairing controls, and counter commands.
- Provide backup/export and restore for gateway configuration. Replacing a gateway must not require manually rebuilding all mappings where a backup exists.
- Prefer the maintained `ESP32Async/ESPAsyncWebServer` stack for static files, REST endpoints, and WebSocket support. The original `me-no-dev` repository is archived.

## MQTT removal and Home Assistant

- Remove MQTT from both the ESP32 gateway and the custom HA integration.
- Home Assistant only needs a local push stream of sensor data from the gateway; pairing and management stay on the gateway website.
- Use one gateway-hosted WebSocket connection per HA config entry. The gateway pushes sensor/binary-sensor payloads to HA.
- Preserve the existing model in which the gateway forwards an opaque binary node payload over WebSocket. Normal over-the-air telemetry contains `frame header + opaque values`, without a repeated profile ID. The WebSocket envelope contains its own version/type plus sender ID, RSSI, and the profile ID looked up from the gateway registration database, followed by the opaque telemetry values. Adding a new profile should require only an HA integration update.
- WebSocket requirements: reconnect with backoff, ping/pong, gateway availability, bounded client queues, and a full state resync after reconnect because WebSocket delivery is not durable.
- Use mDNS/Zeroconf discovery and a Home Assistant Config Flow. Prefer one HA config entry per physical gateway rather than a list of gateway MAC addresses in one entry.
- Create one central WebSocket client in the integration runtime and dispatch received frames to sensor platforms. Do not open separate connections in `sensor.py` and `binary_sensor.py`.
- Move module-level entity stores into per-config-entry runtime data and implement proper unload/connection cleanup.
- Remove the HA manifest dependency on MQTT; keep the integration classification as `local_push`.
- The Home Assistant integration lives in the separate `ha_rfm_gateway`
  repository under `custom_components/rfm_gateway`.

## Remaining design decisions

- Exact commissioning request schedule, RX window, and backoff values must be measured on real hardware.
- Whether the shared commissioning key remains sufficient for the final product threat model or is replaced by per-device secret/QR.
- Exact timing and recovery policy for profile switching must be validated with the node implementation. The current gateway switches profiles rather than time-slicing them.
- Exact WebSocket frame envelope and authentication method.
- LAN-only `ws://` threat model versus HTTPS/WSS or application-level authentication.
- Generic `COMMAND` and `COMMAND_RESULT` byte layouts, followed by the PA6
  command-session runtime on node and gateway.
- Final climate reporting interval. Plain door/counter timing is already fixed:
  one-hour rolling keep-alive and at-most-once-per-minute changing-counter
  telemetry.
- ATtiny1614 BOD mode/threshold must be selected from measured power-failure behaviour and required EEPROM safety; do not assume maximum clock is valid across the entire voltage range.
- The baseline compatibility strategy is stable/unchanging node firmware with protocol compatibility maintained by the gateway. Nodes remain physically reprogrammable through UPDI after opening the enclosure.
- ATtiny1614 supports protected-boot-section self-programming. A possible future OTA design may stage a complete authenticated image in an external I2C EEPROM, then let a protected bootloader rewrite and verify the application Flash. This is not required for the first version and needs a decision based on board space, standby power, bootloader size, and commercial support requirements.
