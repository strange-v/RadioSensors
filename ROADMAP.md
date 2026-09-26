# OSK Sense roadmap

This file contains only unfinished milestones, most important first. Protocol and storage decisions belong in their reference documents.

1. **Nodes:** validate radio power on hardware: target application, fallback after three lost reports, a fixed level the node falls back from, and the return to the wanted level after a node restart.
2. **Gateway:** validate sustained radio traffic.
3. **Nodes:** validate command recovery on hardware: a result lost after the node applied it, and a gateway reboot between delivery and result. Measure the session and commissioning receive windows, settle the 250 ms session window, and add both to POWER.md.
4. **Nodes:** measure the consumption of `binary_sht40` against the battery-life target.
5. **Gateway:** add approved updates from GitHub Releases: verify a signed manifest and asset hash before installing board-specific firmware and its compatible Web UI.
6. **Gateway:** design encrypted migration backup/restore and production recovery flows.
7. **Nodes:** settle the −85…−75 dBm automatic-control window from field data.
8. **Nodes** *(optional)*: measure output power per level on our boards and replace the one-dB-per-level assumption of automatic control. Transmit current per level is in [POWER.md](v2/node/POWER.md#radio-power-levels).
9. **Gateway** *(optional)*: a DIY onboarding mode based on a random commissioning code scoped to one gateway installation. It must be opt-in and provisioned into custom nodes by the builder; it must never become a product-wide key or replace unique factory credentials for pre-provisioned nodes.
