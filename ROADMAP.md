# OSK Sense roadmap

This file contains only unfinished milestones, most important first. Protocol and storage decisions belong in their reference documents.

1. **Gateway:** run a multi-day soak with every node and a WebSocket client, logged by `status_logger.py`: no resets, flat heap, no drops. Decode the core dump of any reset.
2. **Nodes:** measure the consumption of `binary_sht40` against the battery-life target.
3. **Gateway:** add approved updates from GitHub Releases: verify a signed manifest and asset hash before installing board-specific firmware and its compatible Web UI.
4. **Gateway:** design encrypted migration backup/restore and production recovery flows.
5. **Nodes:** settle the −85…−75 dBm automatic-control window from field data.
6. **Nodes** *(optional)*: measure output power per level on our boards and replace the one-dB-per-level assumption of automatic control. Transmit current per level is in [POWER.md](node/POWER.md#radio-power-levels).
7. **Gateway** *(optional)*: a DIY onboarding mode based on a random commissioning code scoped to one gateway installation. It must be opt-in and provisioned into custom nodes by the builder; it must never become a product-wide key or replace unique factory credentials for pre-provisioned nodes.
