# OSK Sense roadmap

This file contains only unfinished milestones. Protocol and storage decisions belong in their reference documents.

## Gateway

- Consider an optional DIY onboarding mode based on a random commissioning code scoped to one gateway installation. It must be opt-in and provisioned into custom nodes by the builder; it must never become a product-wide key or replace unique factory credentials for pre-provisioned nodes.
- Review registry locking. A commit holds the registry mutex across the NVS write in `store.save()`, and `activeProfileId()` takes that same mutex with `portMAX_DELAY` on the telemetry ingest path, so a pairing commit or a rename can block telemetry for the duration of a flash write. The radio receive path is already unaffected: it tests the lock-free active-node bitmap. Measure the worst-case NVS write before deciding whether to widen the lock-free view, shorten the critical section, or leave it.
- Implement durable idempotent commands and the telemetry-ACK pending hint.
- Design encrypted migration backup/restore and production recovery flows.
- Validate sustained radio traffic, OTA coexistence, PBKDF2 timing, watchdog behavior, flash encryption, and secure boot policy.

## Nodes

- Implement the door runtime and climate-equipped door variants. A new door state change must be sent even while radio retry backoff is active.
- Implement PA6 command sessions and 10-second network factory reset while preserving counter state.
- Freeze generic `COMMAND` and `COMMAND_RESULT` payloads and add the ACK pending hint flow.
- Measure climate and door sleep current with the unused-pin configuration.
- Measure commissioning retry/RX-window and command receive durations.

`V1_FEATURE_INVENTORY.md` remains a temporary parity checklist until these node profiles are reviewed, then it should move to archive or be deleted.
