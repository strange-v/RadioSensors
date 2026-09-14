# OSK Sense roadmap

This file contains only unfinished milestones. Protocol and storage decisions belong in their reference documents.

## Gateway

- Consider an optional DIY onboarding mode based on a random commissioning code scoped to one gateway installation. It must be opt-in and provisioned into custom nodes by the builder; it must never become a product-wide key or replace unique factory credentials for pre-provisioned nodes.
- Design encrypted migration backup/restore and production recovery flows.
- Validate sustained radio traffic, OTA coexistence, and watchdog behavior.

## Nodes

- Add `binary_tmp112` and `binary_sht40` with a fixed 5-minute climate interval; every report, periodic or on a state change, carries the full frame. Measure consumption against the battery-life target before settling the interval.
- Validate command recovery on hardware: a result lost after the node applied it, and a gateway reboot between delivery and result. Measure the session and commissioning receive windows, settle the 250 ms session window, and add both to POWER.md.
- **Required: measure and raise the transmit power ceiling per board.** Every image ships with `NODE_RADIO_MAX_POWER_LEVEL=2`. At minimum the supercapacitor climate node must be tested and given a better ceiling: the highest level whose transmit peak the supercapacitor holds with margin above BOD at the low end of its charge. Then the CR2032 boards, with a cell near end of life and in the cold.
- Measure the level → dBm → mA table of the RFM69 HCW on our boards, replace the one-dB-per-level assumption of automatic control, and settle the −85…−75 dBm window from field data.
- Validate radio power on hardware: target application, fallback after three lost reports, and a fixed level the node falls back from.
- Add the supply-limit guard: lower the effective ceiling while the loaded supply voltage after transmission is below a threshold measured per supply, and report `supply_limited`.

`V1_FEATURE_INVENTORY.md` remains a temporary parity checklist until these node profiles are reviewed, then it should move to archive or be deleted.
