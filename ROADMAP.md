# RadioSensors v2 roadmap

This file contains only unfinished milestones. Protocol and storage decisions belong in their reference documents.

## Gateway

- Consider an optional DIY onboarding mode based on a random commissioning
  code scoped to one gateway installation. It must be opt-in and provisioned
  into custom nodes by the builder; it must never become a product-wide key or
  replace unique factory credentials for pre-provisioned nodes.
- Implement durable idempotent commands and the telemetry-ACK pending hint.
- Design encrypted migration backup/restore and production recovery flows.
- Validate sustained radio traffic, Ethernet recovery, OTA coexistence, PBKDF2 timing, watchdog behavior, flash encryption, and secure boot policy.

## Nodes

- Connect and validate the door runtime, then the counter runtime and climate-equipped door variants.
- Implement PA6 command sessions and 10-second network factory reset while preserving counter state.
- Freeze generic `COMMAND` and `COMMAND_RESULT` payloads and add the ACK pending hint flow.
- Measure sleep current and choose the final PA5 low-power configuration.
- Measure commissioning retry/RX-window and command receive durations.

`V1_FEATURE_INVENTORY.md` remains a temporary parity checklist until these node profiles are reviewed, then it should move to archive or be deleted.
