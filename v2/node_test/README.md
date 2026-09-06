# ATtiny1614 v2 test node

This firmware is the bench fixture for commissioning and profile-2 TMP112 telemetry. It is not the production node architecture.

## Bench flow

1. Configure the same 16-byte development commissioning key here and in the gateway's ignored `LocalSecrets.h`.
2. Upload with EEPROM erase for a factory-fresh run.
3. Press the node button to send `JOIN_REQUEST` immediately, or wait for its retry.
4. With an unconfigured gateway, create the first admin through the physical setup flow. With a configured gateway, press Waveshare BOOT to open pairing.
5. Confirm `JOIN_ACCEPT`, `JOIN_CONFIRM`, `JOIN_COMPLETE`, and acknowledged telemetry in the serial logs and gateway `/health` counters.

The assigned node ID, operational network, installation key, nonce, and provisional/active state use alternating CRC-protected EEPROM slots. Reset between `JOIN_ACCEPT` and `JOIN_COMPLETE` to verify that the same transaction resumes.

Upload uses serial UPDI on COM6 at 115200 baud as configured in `platformio.ini`.
