# Gateway node registry

The gateway registry is the source of truth for provisioned node identities. It
has a fixed capacity of 64 records and performs no dynamic allocation.

## Address allocation

| ID | Use |
| ---: | --- |
| 0 | Unprovisioned node during commissioning |
| 1..99 | Allocatable persistent node IDs |
| 100 | Gateway |
| 101..254 | Reserved for future protocol use |
| 255 | Broadcast |

Allocation selects the lowest unused ID in `1..99`. Pending, active, and
disabled records all retain their ID. Only explicit record removal releases it.

## Persistent record

| Offset | Bytes | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 10 | `device_uid` | Fixed tinyAVR factory UID |
| 10 | 1 | `node_id` | Allocated radio address |
| 11 | 2 | `profile_id` | Unsigned little-endian; zero invalid |
| 13 | 3 | Firmware version | Major, minor, patch bytes |
| 16 | 1 | State | 1 pending, 2 active, 3 disabled |
| 17 | 4 | `request_nonce` | Unsigned little-endian; latest commissioning transaction |

The stored record is 21 bytes. Runtime observations such as last-seen time,
RSSI, telemetry, and packet counters are deliberately excluded from persistent
storage.

## State rules

```text
new JoinRequest ──persistent reserve──> pending
pending ──matching UID + node ID + latest nonce──> active
active ──same UID + node ID + latest nonce──> resend Join complete (no write)
pending/active ──explicit management action──> disabled
any state ──explicit removal──> absent (ID reusable)
```

A repeated Join request with the same UID and profile retains its node ID and
updates the pending firmware version and nonce. A different profile for an
existing UID is a conflict and cannot silently change the record. An active or
disabled record cannot be replaced through pairing.

## Snapshot format

```text
+0    magic "RSNR"                    4 bytes
+4    storage schema version (LE)      2 bytes
+6    generation (LE)                  4 bytes
+10   record count                     1 byte
+11   reserved                         1 byte
+12   records                          count * 21 bytes
end   CRC32 over all preceding bytes   4 bytes, little-endian
```

Storage schema version 1 has a maximum snapshot size of 1360 bytes.

NVS contains two blobs, `registry_a` and `registry_b`. A save writes and reads
back the inactive slot before advancing the in-memory generation. Startup
validates both slots and loads the valid snapshot with the newest wrapping
32-bit generation. If the newest slot is incomplete or corrupt, the previous
valid snapshot remains usable.

## Task boundary

NVS reads and writes never run in the priority-11 radio-owner task. The
priority-6 commissioning task performs transactional reserve/confirm operations
and persists a reservation before queuing Join accept. Registry access is
serialized by the gateway store mutex. Future telemetry consumers must use the
synchronized store API rather than retaining pointers into the registry.
