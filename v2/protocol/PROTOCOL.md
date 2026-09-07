# RadioSensors v2 radio protocol

This document is the canonical byte-level description of the v2 application
protocol. Multi-byte integer layouts, message payloads, and telemetry profiles
must be added here before their codec implementation is merged.

## Protocol layers

```text
RFM69 / LowPowerLab frame
+------------+-----------+---------+------------------------------+
| target ID  | sender ID | control | DATA[] application frame     |
+------------+-----------+---------+------------------------------+
                                      |
                                      +-- DATA[0]: v2 frame header
                                      +-- DATA[1..N]: kind payload
```

Target ID, sender ID, ACK flags, length, and RSSI are transport metadata exposed
by the RFM69 driver. They are not repeated in `DATA[]`.

## Common application header

Every v2 application frame starts with exactly one byte.

```text
DATA[0]
bit      7       6       5       4       3       2       1       0
     +-------+-------+-------+-------+-------+-------+-------+-------+
     |       protocol_major |               frame_kind              |
     +-------+-------+-------+-------+-------+-------+-------+-------+
             3 bits (2)                     5 bits
```

| Bits | Mask | Field | Meaning |
| --- | --- | --- | --- |
| 7..5 | `0xE0` | `protocol_major` | Current value is `2` |
| 4..0 | `0x1F` | `frame_kind` | Message layout selector |

Unknown versions and reserved kinds are rejected. Payload length and field
validation belong to the codec for the selected kind.

## Frame kinds

| Value | Header | Name | Payload ownership |
| ---: | ---: | --- | --- |
| 0 | `0x40` | Telemetry | Opaque; decoded by a consumer using the registry profile |
| 1 | `0x41` | Join request | Gateway commissioning codec |
| 2 | `0x42` | Join accept | Node commissioning codec |
| 3 | `0x43` | Join confirm | Gateway commissioning codec |
| 4 | `0x44` | Command | Generic command envelope plus command-specific data |
| 5 | `0x45` | Command result | Generic result envelope plus command-specific data |
| 6 | `0x46` | Error | Common control-plane error |
| 7 | `0x47` | Join complete | Node commissioning codec |
| 8 | `0x48` | Command ready | Node opens an explicit command session |
| 9 | `0x49` | No command | Gateway closes an empty command session |
| 10..31 | — | Reserved | Must not be transmitted in protocol major 2 |

Join request, Join accept, Join confirm, and Join complete are frozen below.
Command ready and No command are frozen below. Command, command result, and
error payload layouts remain unassigned; reserving their kind values does not
freeze an incomplete payload design.

## Sleeping-node command session

Normal telemetry opens only the short RFM69 acknowledgement period. A gateway
never attempts to push an application command to a sleeping node. The user
first queues one persistent state-changing command for a selected node in the
gateway UI, then physically short-presses the node button.

After button release, the node sends Command ready from its operational node ID
and radio profile, then listens for a bounded command window. The frame is
exactly five bytes:

| Offset | Bytes | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Common header | `0x48` |
| 1 | 4 | `session_nonce` | Node-generated unsigned little-endian value |

The gateway uses the RFM69 sender ID to select that node's pending command. A
delivered Command must echo the session nonce so a delayed response from an old
button session cannot be accepted. Only one state-changing command may be
pending for a node and only one is delivered per button session.

If no command is pending, the gateway immediately sends No command. Its layout
is identical except for header `0x49`; it echoes the same session nonce. This
lets the node close its receive window without waiting for timeout.

The node durably applies a command and records its command ID before sending
Command result. The gateway marks the pending command complete only after a
matching result is durably recorded. If the result is lost, a later button
session delivers the same command ID and payload. The node recognizes the
duplicate, does not reapply it, and resends the stored result with the new
session nonce.

## Telemetry

```text
+----------------------+---------------------------------------------+
| DATA[0] = 0x40       | DATA[1..N]                                  |
| v2 / Telemetry       | opaque telemetry payload                    |
+----------------------+---------------------------------------------+
```

Telemetry does **not** carry a profile ID. The gateway uses the RFM69 transport
sender ID to look up the registration record:

```text
RFM69 sender ID ----> gateway registry ----> profile ID ------------+
                                                                   |
DATA[1..N] opaque payload ------------------------------------------+--> HA decoder
```

The gateway forwards the stored profile ID with sender ID, RSSI, and the opaque
payload in its future WebSocket envelope. Telemetry from an unknown or inactive
sender cannot be decoded safely and must be rejected and counted.

Registration stores one stable numeric profile ID. The profile defines the
complete node contract: telemetry layout, logical category, supported commands,
and Home Assistant entities. A wire-incompatible telemetry layout or different
command set requires a new profile ID. The profile ID is not repeated in normal
telemetry.

## Join request

An unprovisioned node transmits a join request with RFM69 sender ID `0` while the
gateway's explicit pairing window is open. Protocol major 2 supports tinyAVR
devices with the fixed 10-byte factory serial number in `SIGROW.SERNUM[9:0]`.

```text
DATA offset
  0       +-----------+  0x41: protocol major 2 / JoinRequest
  1..10   | UID       |  factory device UID, 10 bytes in SIGROW order
 11..12   | profile   |  profile_id, uint16 little-endian
 13       | fw major  |  firmware semantic-version major
 14       | fw minor  |  firmware semantic-version minor
 15       | fw patch  |  firmware semantic-version patch
 16..19   | nonce     |  request_nonce, uint32 little-endian
          +-----------+
```

| Offset | Bytes | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Common header | Must be `0x41` |
| 1 | 10 | `device_uid` | Opaque bytes, fixed length |
| 11 | 2 | `profile_id` | Unsigned little-endian; zero is reserved and invalid |
| 13 | 1 | `firmware.major` | Unsigned byte |
| 14 | 1 | `firmware.minor` | Unsigned byte |
| 15 | 1 | `firmware.patch` | Unsigned byte |
| 16 | 4 | `request_nonce` | Unsigned little-endian |

The application frame length must be exactly 20 bytes. The nonce correlates a
future join accept with the current request; all 32-bit values, including zero,
are valid. Authentication and replay resistance depend on the commissioning
security profile and are not provided by the public UID.

Known vector:

```text
41 10 21 32 43 54 65 76 87 98 A9 34 12 01 02 03 EF CD AB 89
|  |--------------------------| |---| |------| |-----------|
H            UID               1234   1.2.3     89ABCDEF
```

## Join accept

The gateway sends Join accept to transport address `0` under the commissioning
radio profile. UID and nonce select the intended unprovisioned node. The frame
is sent only after the pending registry reservation is durable.

| Offset | Bytes | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Common header | `0x42` |
| 1 | 10 | `device_uid` | Echoed from Join request |
| 11 | 4 | `request_nonce` | Echoed, uint32 little-endian |
| 15 | 1 | `assigned_node_id` | Persistent address; not 0, gateway, or 255 |
| 16 | 1 | `gateway_node_id` | Operational gateway address |
| 17 | 1 | `network_id` | Operational RFM69 network ID |
| 18 | 16 | `installation_key` | Raw operational RFM69 AES key |

The exact application frame length is 34 bytes. Frequency, bitrate, and default
node transmit power are profile/firmware constants and are not repeated here.
The installation key is generated randomly during initial gateway setup. It is
the operational RFM69 AES network key for one gateway installation and is
therefore shared by the nodes joined to that gateway; it is not a product-wide,
build-time, or public commissioning key. It is protected on air only by the
commissioning radio profile. Each node has a unique factory key supplied
together with its UID; the gateway loads it into the radio only for that UID's
pairing transaction and wipes it afterwards. RFM69 AES encryption is not an
authenticated key-exchange protocol, which is an explicitly accepted
limitation of this design.

## Join confirm

After atomically storing the operational configuration, the node switches to
its assigned transport address and operational profile, then sends:

| Offset | Bytes | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Common header | `0x43` |
| 1 | 10 | `device_uid` | Factory UID |
| 11 | 4 | `request_nonce` | Accepted request nonce, uint32 little-endian |

The exact application frame length is 15 bytes. The gateway requires the
transport sender ID, UID, and latest persisted nonce to match before changing
the registry record from pending to active.

## Join complete

After the active registry state is durably stored, the gateway sends a
15-byte Join complete frame to the node's assigned operational address. Its
layout is identical to Join confirm except that the header is `0x47`. UID and
nonce therefore acknowledge the exact commissioning transaction.

The node must not consider commissioning complete until it receives this
frame. It may repeat Join confirm while waiting. The gateway retains the last
successful nonce and responds to a duplicate matching confirm without changing
or rewriting the registry. A wrong nonce is rejected.

## Telemetry profile template

Each schema added below must define all of the following:

| Item | Required definition |
| --- | --- |
| Identity | Stable numeric profile ID and human-readable profile name |
| Length | Exact payload length, excluding `DATA[0]` |
| Fields | Byte offset, byte count, signedness, and byte order |
| Meaning | Scale, unit, valid range, and sentinel values |
| Test vector | Complete application frame in hexadecimal and expected values |

### Production profile numbering

V2 profile IDs are stable opaque keys allocated sequentially. Their numeric
values do not encode a capability family, hardware type, or Home Assistant
presentation. IDs are never reused after release. Profile 0 remains invalid.

| Profile ID | Meaning | Telemetry fields |
| ---: | --- | --- |
| 1 | Supply voltage | VCC |
| 2 | Temperature | Temperature, VCC |
| 3 | Temperature and humidity | Temperature, humidity, VCC |
| 4 | Temperature, humidity, and pressure | Temperature, humidity, pressure, VCC |
| 5 | Binary input | State, VCC |
| 6 | Pulse counter | Counter, VCC |
| 7 | Binary input with climate sensor | State, temperature, humidity, VCC |
| 8 | Binary input with temperature sensor | State, temperature, VCC |

Only profiles 1 and 2 are frozen below. The remaining IDs reserve the initial
V2 catalogue entries, but their byte layouts and known vectors must be
specified here before a V2 node transmits them. Gas/water and door/window are
installation presentation, not different wire profiles.

### Profile 1: supply voltage

Profile ID `1` carries only the node supply voltage. Its telemetry payload is
exactly two bytes (three bytes including the common header):

| DATA offset | Bytes | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Common header | `0x40` |
| 1 | 2 | Supply voltage | unsigned little-endian millivolts |

Example for 3300 mV: `40 E4 0C`.

### Profile 2: temperature test node

Profile ID `2` is used by the ATtiny1614/TMP112 commissioning test node. Its
telemetry payload is exactly four bytes (five bytes including the common
header):

| DATA offset | Bytes | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Common header | `0x40` |
| 1 | 2 | Temperature | signed little-endian, degrees C x 100; `INT16_MIN` means unavailable |
| 3 | 2 | Supply voltage | unsigned little-endian millivolts |

Example for 23.50 degrees C and 3300 mV: `40 2E 09 E4 0C`.

The legacy type byte in v1 payload structs is not copied into v2 telemetry.

### Profile 5: binary input

The application frame is exactly four bytes: header, one-byte state (`0` or
`1`), and unsigned little-endian VCC in millivolts. Example for state `1` and
3300 mV: `40 01 E4 0C`.

### Profile 6: pulse counter

The application frame is exactly seven bytes: header, unsigned 32-bit
little-endian cumulative pulse count, and unsigned little-endian VCC in
millivolts. Example for count `0x12345678` and 3300 mV:
`40 78 56 34 12 E4 0C`.

Gas/water meaning, units per pulse, and display unit are installation metadata.
They are not part of this telemetry frame. The `SET_COUNT` command is part of
this profile, but its command envelope remains unfrozen.

### Profile 7: binary input with SHT40 climate data

The application frame is exactly eight bytes: header, state (`0` or `1`),
signed little-endian temperature in degrees C x 100, unsigned little-endian
humidity in percent RH x 100, and unsigned little-endian VCC in millivolts.
`INT16_MIN` means unavailable temperature; `UINT16_MAX` means unavailable
humidity. Example for state `1`, 23.50 degrees C, 45.67% RH, and 3300 mV:
`40 01 2E 09 D7 11 E4 0C`.

### Profile 8: binary input with TMP112 temperature

The application frame is exactly six bytes: header, state (`0` or `1`), signed
little-endian temperature in degrees C x 100, and unsigned little-endian VCC in
millivolts. `INT16_MIN` means unavailable temperature. Example for state `1`,
23.50 degrees C, and 3300 mV: `40 01 2E 09 E4 0C`.

## Codec invariants

- No packed C/C++ structs are transmitted directly.
- Every multi-byte value has explicit byte order and fixed width.
- Encoding and decoding do not allocate memory.
- The one-byte common codec does not interpret opaque telemetry bytes.
- Documented hexadecimal examples are mirrored by native unit tests.
