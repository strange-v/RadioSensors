# Node EEPROM allocation

ATtiny1614 provides 256 bytes of EEPROM organized in 32-byte physical pages.

## Common nodes

All node profiles reserve two independently validated 32-byte network
configuration slots:

| Address | Size | Purpose |
| --- | ---: | --- |
| `0x00..0x1F` | 32 | Network configuration slot A |
| `0x20..0x3F` | 32 | Network configuration slot B |
| `0x40..0xFF` | 192 | Profile-owned or unused |

Each network configuration slot has this exact format:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Magic `RN` |
| 2 | 1 | Storage schema version (`1`) |
| 3 | 1 | Wrapping generation |
| 4 | 1 | Provisioning state in bits 7..5, radio power in bits 4..0 |
| 5 | 1 | Node ID |
| 6 | 1 | Gateway ID |
| 7 | 1 | Network ID |
| 8 | 16 | Installation key |
| 24 | 4 | Request nonce, little-endian |
| 28 | 2 | Last applied radio-power command ID, little-endian |
| 30 | 2 | CRC16-CCITT over bytes 0..29, little-endian |

Profile ID, firmware version, sensor selection, pins, and reporting intervals
are compile-time values and are not stored here. UID comes from `SIGROW`.

Saving always targets the older/inactive slot. Its magic is invalidated first,
the payload and CRC are written next, and the two magic bytes are committed
last. On boot, both slots are validated and the newest generation is selected,
including across the 8-bit generation wrap.

Factory reset invalidates only the two common configuration slots.

## Counter node

The counter profile divides its 192-byte application area by write frequency:

| Address | Size | Purpose |
| --- | ---: | --- |
| `0x40..0x4F` | 16 | Last `SET_COUNT` result slot A |
| `0x50..0x5F` | 16 | Last `SET_COUNT` result slot B |
| `0x60..0xFF` | 160 | Wear-levelled cumulative-count ring |

Each `SET_COUNT` result slot has this exact format:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Commit magic (`0xC7`) |
| 1 | 1 | Storage schema version (`1`) |
| 2 | 1 | Wrapping generation |
| 3 | 1 | Status: `Pending` or `Applied` |
| 4 | 2 | Command ID, little-endian |
| 6 | 4 | Count before the command, little-endian |
| 10 | 4 | Requested/applied count, little-endian |
| 14 | 2 | CRC16-CCITT over bytes 0..13, little-endian |

The result slots use the same invalidate/payload/commit sequence as network
configuration. Applying `SET_COUNT` is a recoverable three-step operation:

1. Persist a `Pending` result with the command ID, old count, and requested
   count.
2. Persist the requested value in the counter ring.
3. Persist the result as `Applied`, then send the command result.

On boot, a valid `Pending` result is completed before new pulses are accepted.
Repeating a command with the same ID returns the stored result instead of
changing the count again.

Each ring entry contains a one-byte sequence followed by a four-byte unsigned
cumulative count. Sequence `0xFF` means invalid/uncommitted; valid sequences
wrap from `0xFE` to `0x00`. A save invalidates the destination sequence byte,
writes the count, then commits the sequence byte last. There are 32 entries.

Every confirmed LOW-to-HIGH transition increments the count and immediately
persists it before telemetry transmission. Capacity planning uses 150,000
pulses/year to include higher winter gas consumption. A 32-entry ring therefore
spreads approximately 4,688 writes per cell per year. Against the device's
100,000-cycle minimum EEPROM endurance, this is about 21.3 years of nominal
minimum endurance (3.2 million persisted pulses total).

The counter area survives network factory reset.

## Native verification

The host-side tests cover CRC fallback, interrupted records, both generation
wraps, factory-reset isolation, and multiple counter-ring rotations:

```sh
wsl bash v2/node/scripts/run_native_tests_wsl.sh
```
