# ATtiny1614 v2 test node

This firmware exercises the complete v2 commissioning transaction against the
gateway and then sends profile-2 TMP112 telemetry.

## Bench flow

1. Set the same 16-byte commissioning key in this project's build flags and in
   the gateway's ignored `LocalSecrets.h`.
2. Flash the node. A blank EEPROM means unprovisioned; the node transmits from
   ID 0 on commissioning network 0 every five seconds.
3. Press BOOT on the Waveshare gateway to open pairing.
4. Watch the node serial log for `Accepted node ID` and `Commissioning complete`.
   Gateway `/health` should increment `nodesActivated` and return to the
   operational profile.

The assigned address, operational network, installation key, request nonce,
and provisional/active state are CRC-protected in alternating EEPROM slots.
A reset between JOIN_ACCEPT and JOIN_COMPLETE resumes JOIN_CONFIRM without
allocating a different node ID.

For repeated factory-fresh tests, erase the ATtiny1614 EEPROM when uploading.
The production button-driven factory reset is intentionally not part of this
bench firmware until the node board's button pin and counter-retention policy
are fixed.

## Firmware structure recommendation

Keep one shared node platform layer for commissioning, EEPROM configuration,
radio profile switching, command envelopes, sleep scheduling, diagnostics, and
firmware versioning. Build a separate image per stable node profile (for
example climate, binary input, or counter), with the profile selecting sensor
drivers, pins, telemetry encoding, wake policy, and supported commands at
compile time.

This gives all node types one maintained protocol implementation without
shipping every sensor driver and every hardware branch in every small MCU
image. Use build environments as product variants; use runtime configuration
only for installation values and genuinely adjustable settings such as radio
power. A wire-incompatible payload or command set gets a new profile ID.
