# RadioSensors Architecture Handoff

Gateway firmware implementation progress is tracked separately in
[`v2/gateway/HANDOFF.md`](v2/gateway/HANDOFF.md).

## Project context

- Existing nodes use ATmega328P; all new node designs will use ATtiny1614.
- Some node types must retain state, especially pulse counters for gas/water meters.
- Counter persistence currently uses EEWL wear levelling. Preserve this approach and re-check exactly when `node_counter` writes after the firmware changes.
- New outdoor nodes may be conformal-coated, solar powered, and connected to a supercapacitor, so physical access and continuous RX are not assumed.

## Node identity and provisioning

- Ship every new node with operational `NODE_ID = 0` (unprovisioned state).
- New node boards have a physical button. A short press opens an explicit receive/configuration window when supported by that node type. Holding it for more than 10 seconds performs a factory reset/reprovisioning reset. Counter state should be treated separately from network configuration so an accidental factory reset does not silently erase the measured total unless explicitly designed otherwise.
- Use the ATtiny1614 factory 10-byte UID during provisioning. Keep the compact node ID in normal radio packets.
- An unprovisioned node periodically sends a registration request containing at least UID, node type, firmware/protocol version, capabilities, and a request nonce.
- Do not listen for five seconds every minute: RFM69 RX current makes this far too expensive. Use a short response window, randomized jitter, and exponential backoff. A button/reed switch may trigger a faster registration cycle where available.
- Gateway pairing mode is explicitly enabled by the user. With one RFM69, the gateway may temporarily switch between the commissioning and operational radio profiles.
- Approval reserves a stable `UID -> NODE_ID` mapping. Repeated requests from the same UID must receive the same reserved ID.
- `JOIN_ACCEPT` should contain the UID, echoed nonce, assigned node ID, and operational network configuration.
- The node validates UID/nonce, writes the configuration atomically, switches to its assigned ID, and sends `JOIN_CONFIRM` from that ID. The gateway marks it active only after confirmation.
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
- Decide before implementation whether the product threat model requires the per-device secret/QR flow or whether the shared commissioning key is sufficient.

## Counter commands

- Gateway UI will support an absolute `SET_COUNT` command so a counter node can be initialized from the real meter value.
- Gateway UI will also support changing a node's `POWER_LEVEL`; the node persists the new value and confirms the applied setting.
- Include a monotonically increasing command sequence/ID. The gateway persists the pending command and retries the exact same command ID and payload when its response is lost.
- The node must persist the accepted command ID atomically with the updated counter state before replying. A duplicate command must not apply `SET_COUNT` again because pulses may have arrived since the first application; it only resends the previous result/current state. Reject commands older than the last accepted sequence.
- Do not add command IDs, revisions, or other provisioning metadata to regular telemetry. Keeping every normal radio transmission as small as possible is a core energy-efficiency requirement. Command IDs exist only in the rare command/response exchange.
- Node response should include command ID, old value, applied value/current value, and success/failure.
- Allow only one outstanding state-changing command per node. Gateway considers the command complete only after a matching command response. If it is lost, the gateway retries the same command later; the node recognizes the duplicate, does not reapply it, and sends the response again. The UI must show an explicit old-to-new confirmation before sending.

## Gateway redesign

- Keep telemetry payloads opaque to the gateway. During registration it stores the numeric node type/schema ID alongside the UID and assigned node ID. It may attach that stored type plus transport metadata such as sender ID and RSSI, but it must not decode sensor fields or require firmware changes when a new node telemetry type is introduced. Telemetry schemas remain owned by the HA integration (and other consumers).
- Every application radio packet starts with a one-byte frame header/kind. Prefer encoding a small protocol-major version and frame kind in the same byte. Kinds cover telemetry, join request/accept/confirm, command, command result, and error. This byte replaces the old repeated node-type byte, so normal telemetry does not grow.
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
- Preserve the existing model in which the gateway forwards an opaque binary node payload over WebSocket. Normal over-the-air telemetry contains `frame header + opaque values`, not the repeated node type. The WebSocket envelope contains its own version/type plus sender ID, RSSI, and the node type/schema ID looked up from the gateway registration database, followed by the opaque telemetry values. Adding a new telemetry type should require only an HA integration update.
- WebSocket requirements: reconnect with backoff, ping/pong, gateway availability, bounded client queues, and a full state resync after reconnect because WebSocket delivery is not durable.
- Use mDNS/Zeroconf discovery and a Home Assistant Config Flow. Prefer one HA config entry per physical gateway rather than a list of gateway MAC addresses in one entry.
- Create one central WebSocket client in the integration runtime and dispatch received frames to sensor platforms. Do not open separate connections in `sensor.py` and `binary_sensor.py`.
- Move module-level entity stores into per-config-entry runtime data and implement proper unload/connection cleanup.
- Remove the HA manifest dependency on MQTT; keep the integration classification as `local_push`.
- The Home Assistant integration lives in the separate `ha_rfm_gateway`
  repository under `custom_components/rfm_gateway`.

## Remaining design decisions

- Exact commissioning request schedule, RX window, and backoff values must be measured on real hardware.
- Shared commissioning key versus per-device secret/QR.
- Whether pairing temporarily interrupts the operational radio profile or the gateway time-slices between both profiles.
- Exact WebSocket frame envelope and authentication method.
- LAN-only `ws://` threat model versus HTTPS/WSS or application-level authentication.
- EEPROM configuration layout and migration from hard-coded legacy settings.
- ATtiny1614 BOD mode/threshold must be selected from measured power-failure behaviour and required EEPROM safety; do not assume maximum clock is valid across the entire voltage range.
- The baseline compatibility strategy is stable/unchanging node firmware with protocol compatibility maintained by the gateway. Nodes remain physically reprogrammable through UPDI after opening the enclosure.
- ATtiny1614 supports protected-boot-section self-programming. A possible future OTA design may stage a complete authenticated image in an external I2C EEPROM, then let a protected bootloader rewrite and verify the application Flash. This is not required for the first version and needs a decision based on board space, standby power, bootloader size, and commercial support requirements.
