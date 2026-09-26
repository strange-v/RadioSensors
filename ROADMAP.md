# OSK Sense roadmap

This file contains only unfinished milestones. Protocol and storage decisions belong in their reference documents.

## Gateway

- Consider an optional DIY onboarding mode based on a random commissioning code scoped to one gateway installation. It must be opt-in and provisioned into custom nodes by the builder; it must never become a product-wide key or replace unique factory credentials for pre-provisioned nodes.
- Design encrypted migration backup/restore and production recovery flows.
- Add approved updates from GitHub Releases: verify a signed manifest and asset hash before installing board-specific firmware and its compatible Web UI.
- Validate sustained radio traffic, OTA coexistence, and watchdog behavior.

## Nodes

- Add `binary_tmp112` with a fixed climate interval and measure consumption of both binary-climate images against the battery-life target.
- Validate command recovery on hardware: a result lost after the node applied it, and a gateway reboot between delivery and result. Measure the session and commissioning receive windows, settle the 250 ms session window, and add both to POWER.md.
- Measure the level → dBm → mA table of the RFM69 HCW on our boards, replace the one-dB-per-level assumption of automatic control, and settle the −85…−75 dBm window from field data.
- Validate radio power on hardware: target application, fallback after three lost reports, a fixed level the node falls back from, and the return to the wanted level after a node restart.
- Add the supply-limit guard: lower the effective ceiling while the loaded supply voltage after transmission is below a per-supply threshold taken from the [supply sag](v2/node/POWER.md#supply-sag), and report `supply_limited`.

`V1_FEATURE_INVENTORY.md` remains a temporary parity checklist until these node profiles are reviewed, then it should move to archive or be deleted.
