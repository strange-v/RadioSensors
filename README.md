# OSK Sense

OSK Sense is a local, low-power sensor system built around ATtiny1614 nodes, RFM69 radio, an Ethernet ESP32 gateway, and a Home Assistant integration. The current development line is v2; legacy firmware remains under `v1/` only as a migration reference.

## Repository map

- `v2/node` — production node firmware, one build per telemetry profile.
- `v2/node_test` — ATtiny1614 bench and commissioning firmware.
- `v2/gateway` — ESP32 gateway for WT32-ETH01 and Waveshare ESP32-S3-ETH.
- `v2/shared/RadioProtocol` — portable wire and persistence codecs.
- `v2/protocol` — canonical radio protocol specification and native tests.

The Home Assistant integration is maintained in the separate `osk-sense-ha` repository.

System boundaries and design decisions are in [ARCHITECTURE.md](ARCHITECTURE.md). The short list of unfinished work is in [ROADMAP.md](ROADMAP.md).

Protocol references:

- [RFM69 application protocol and node payloads](v2/protocol/PROTOCOL.md)
- [Machine-readable client protocol](v2/protocol/protocol-manifest.json)
- [Cross-language protocol vectors](v2/protocol/protocol-vectors.json)
- [Gateway REST API](v2/gateway/API.md)
- [Gateway binary WebSocket protocol](v2/gateway/WEBSOCKET.md)
- [Gateway persistent formats](v2/gateway/STORAGE.md)
- [Node EEPROM formats](v2/node/EEPROM.md)

## Verification

From the repository root:

```powershell
node v2/protocol/scripts/validate_protocol_artifacts.mjs
node v2/protocol/scripts/generate_protocol_docs.mjs
wsl bash v2/protocol/scripts/run_native_tests_wsl.sh
wsl bash v2/node/scripts/run_native_tests_wsl.sh
```

Enable the tracked pre-commit hook once per clone:

```powershell
git config core.hooksPath .githooks
```

The hook validates changed protocol artifacts. It regenerates diagrams only when the staged manifest or diagram generator changed, and stops the commit if the generated SVG files need to be staged.

Gateway and node build commands are documented in their respective READMEs.
