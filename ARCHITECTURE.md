# OSK Sense architecture

## System overview

ATtiny1614 nodes send compact binary measurements over encrypted RFM69. The Ethernet ESP32 gateway owns node registration, acknowledges accepted telemetry, timestamps it in UTC, caches the latest frame per node, and exposes local REST and WebSocket interfaces. Home Assistant is read-only; pairing, users, settings, commands, and recovery belong to the gateway management interface.

```text
sensor node -- encrypted RFM69 --> gateway -- REST/WebSocket --> Home Assistant
```

Telemetry profiles define wire-level measurements. Labels such as gas, water, front door, or bedroom are installation metadata, not profile IDs.

## Node model

Each ATtiny1614 image is statically composed for one stable profile. Shared code owns commissioning, radio, EEPROM, power, scheduling, and commands; profile code owns acquisition and telemetry encoding. There is no dynamic feature registry on the 16 KiB Flash/2 KiB RAM target.

An unconfigured production node uses address and commissioning network ID 0 and has a unique 16-byte factory key supplied with its UID in a QR credential. The gateway keeps that key only in RAM during pairing. Successful commissioning assigns a persistent node ID, operational network ID, and installation key. Network configuration and counter state use separate EEPROM domains.

Solar climate nodes use nominal 60/300-second reporting above/below 2500 mV; the 32-second RTC step gives accepted effective intervals of about 64/320 seconds. Battery climate intervals are compile-time settings. Door and counter nodes are event-driven with a rolling one-hour keep-alive. Counter reports may coalesce, but every confirmed pulse is persisted immediately.

## Radio and registry model

The gateway uses address 100. IDs 1..99 are allocated persistently; 0 is for commissioning and 255 is broadcast. Registry records hold UID, node ID, profile, firmware, commissioning nonce, and pending/active/disabled state.

Commissioning and operational radio profiles have separate AES keys. Initial gateway setup generates an operational network ID in 1..255 and permits an advanced edit before confirmation. Ordinary changes are locked after a node is active; later changes require a staged migration design.

Normal telemetry is `common header + opaque profile payload`; the gateway does not decode measurements. Exact radio bytes are specified only in [v2/protocol/PROTOCOL.md](v2/protocol/PROTOCOL.md).

Sleeping-node commands use a pull session. A telemetry ACK may eventually carry a pending-command hint; the node then sends nonce-bound `COMMAND_READY` and receives one durable command or `NO_COMMAND`. Commands and results must be idempotent and durable before acknowledgement.

## Gateway runtime

One high-priority task exclusively owns RFM69 and its FIFO. The ISR only signals bounded queues. Registry persistence and commissioning run outside that task. A lock-free active-node bitmap allows timely ACK decisions without an NVS lock.

The gateway caches only the latest telemetry frame per node in RAM. Frames from different nodes survive a temporary consumer disconnect and appear in the next snapshot; repeated frames from one node collapse to the latest. Reboot clears the cache. Receive timestamps are UTC from SNTP; zero means unsynchronized.

REST carries identity, registry metadata, settings, and management. The binary WebSocket carries snapshots and live telemetry. Home Assistant uses one scoped read-only token and one WebSocket connection per gateway.

## Persistence and security

Settings, users/tokens, node registry, and installation secrets have separate NVS ownership. Mutable data uses explicit versioned codecs, CRC32, generations, alternating slots, and read-back validation. Existing invalid secret data is never mistaken for an empty store.

Passwords use salted PBKDF2-HMAC-SHA256. API tokens contain 32 random bytes, are shown once, and are stored only as SHA-256 digests. Radio keys, password hashes, token hashes, and the device secret are excluded from normal APIs and diagnostic exports.

HTTP authentication does not protect credentials from LAN packet capture. TLS/WSS or an explicitly trusted LAN, plus flash encryption and secure boot, remain production threat-model decisions.

## Documentation ownership

- Radio frames and payload validation: `v2/protocol/PROTOCOL.md`.
- REST endpoints and JSON: `v2/gateway/API.md`.
- WebSocket bytes and resynchronization: `v2/gateway/WEBSOCKET.md`.
- Persistent bytes: `v2/gateway/STORAGE.md` and `v2/node/EEPROM.md`.
- Build, upload, wiring, and bench use: component README files.
- Unfinished work only: `ROADMAP.md`.

Implementation history, test counts, firmware sizes, and completed iteration logs are deliberately not maintained as architecture documentation.
