# RadioSensors v2 roadmap

This file contains only unfinished milestones. Protocol and storage decisions belong in their reference documents.

## Gateway

- Add the embedded initial-setup Web UI over the physically gated `/api/v1/setup` backend.
- Complete login/session authentication, users, API tokens, settings, stable and boot identity, `/api/v1/info`, `/api/v1/nodes`, authenticated WebSocket, and mDNS discovery.
- Implement transaction-only QR commissioning credentials.
- Implement durable idempotent commands and the telemetry-ACK pending hint.
- Design encrypted migration backup/restore and production recovery flows.
- Validate sustained radio traffic, Ethernet recovery, OTA coexistence, PBKDF2 timing, watchdog behavior, flash encryption, and secure boot policy.

## Nodes

- Connect and validate the door runtime, then the counter runtime and climate-equipped door variants.
- Implement PA6 command sessions and 10-second network factory reset while preserving counter state.
- Freeze generic `COMMAND` and `COMMAND_RESULT` payloads and add the ACK pending hint flow.
- Measure sleep current and choose the final PA5 low-power configuration.
- Measure commissioning retry/RX-window and command receive durations.
- Replace development keys with production UID/key provisioning.

`V1_FEATURE_INVENTORY.md` remains a temporary parity checklist until these node profiles are reviewed, then it should move to archive or be deleted.
