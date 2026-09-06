# Gateway persistent storage layouts

This document is the byte-level source of truth for gateway settings, authentication, node registry, and installation-secret persistence. Schema 1 is frozen by the portable codecs in `../shared/RadioProtocol` and their native known-layout, round-trip, validation, corruption-recovery, and interrupted-write tests. ESP32 NVS adapters and runtime ownership are implemented by `ConfigurationStore` and `NodeRegistryStore`.

All multi-byte integers are unsigned little-endian unless stated otherwise. No
compiler structs are persisted directly. Reserved bytes and unused fixed
records are encoded as zero and must be zero when decoding schema version 1.

## Common dual-slot rules

Each store owns two NVS blobs. A save serializes the complete next generation
into the inactive slot, reads it back, validates every field and its CRC32, and
only then publishes it as current. The previous valid generation remains
recoverable after interruption or corruption.

Every snapshot begins with:

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | Store-specific ASCII magic |
| 4 | 2 | Storage schema version, initially `1` |
| 6 | 4 | Wrapping generation |
| 10 | 2 | Exact total encoded size including CRC |

The final four bytes are CRC32 over every preceding byte, encoded little-endian.
Loading validates both slots and selects the newer valid wrapping generation by
the same comparison rule as the node registry. An absent store loads documented
defaults at generation zero; it does not write merely because the gateway
booted.

Generation advances only after a durable semantic change. Re-saving identical
content is a no-op. Readers receive copies or immutable published snapshots and
never retain pointers into mutable store memory.

## Gateway settings snapshot

NVS namespace: `gateway-config`; slot keys: `config_a`, `config_b`; magic:
`RSGC`; exact schema-1 size: 264 bytes.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 12 | Common snapshot header |
| 12 | 1 | Flags |
| 13 | 1 | Display-name byte length, `0..48` |
| 14 | 1 | NTP server count, `0..3` |
| 15 | 1 | Reserved, zero |
| 16 | 2 | Node-pairing window seconds |
| 18 | 2 | Initial-admin setup window seconds |
| 20 | 48 | Display-name UTF-8 bytes, zero-padded |
| 68 | 192 | Three fixed 64-byte NTP server entries |
| 260 | 4 | CRC32 |

Flag bit 0 enables mDNS and bit 1 enables NTP; bits 2..7 are zero. Each NTP
entry is one ASCII length byte followed by 63 zero-padded bytes. Entries after
`ntp_server_count` are entirely zero. Accepted values are DNS hostnames or
textual IP addresses without scheme, path, or port. Empty active entries,
embedded NULs, control characters, duplicates, and values longer than 63 bytes
are rejected. When NTP is enabled, count is `1..3`; when disabled, a saved list
may remain.

Display name is optional valid UTF-8 without NUL or control characters. Pairing
duration is `30..900` seconds and defaults to 120. Initial setup duration is
`60..1800` seconds and defaults to 600.

Generation-zero defaults are an empty display name, enabled mDNS and NTP, NTP
servers `pool.ntp.org` and `time.cloudflare.com`, a 120-second pairing window,
and a 600-second setup window. NTP changes take effect without reboot. Timezone
is not stored because the gateway uses UTC. DHCP, telemetry cache, current
clock, uptime, boot ID, and diagnostic counters are runtime state.

## Authentication snapshot

NVS namespace: `gateway-auth`; slot keys: `auth_a`, `auth_b`; magic: `RSAU`;
exact schema-1 size: 1036 bytes.

It contains four fixed user slots and eight fixed API-token slots. Active
records occupy the first counted slots; every remaining slot is zero.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 12 | Common snapshot header |
| 12 | 1 | User count, `0..4` |
| 13 | 1 | API-token count, `0..8` |
| 14 | 2 | Reserved, zero |
| 16 | 4 | Next user ID |
| 20 | 4 | Next token ID |
| 24 | 368 | Four 92-byte user records |
| 392 | 640 | Eight 80-byte token records |
| 1032 | 4 | CRC32 |

IDs are nonzero and unique within their record type. Next IDs are never-issued
values; deletion does not reuse IDs. Zero is normalized to one before the first
allocation. Exhaustion after `UINT32_MAX` is an error rather than silent reuse.

### User record

Exact size: 92 bytes.

| Relative offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | User ID |
| 4 | 1 | Username length, `1..32` |
| 5 | 32 | Normalized username, zero-padded ASCII |
| 37 | 1 | Role |
| 38 | 1 | Flags |
| 39 | 1 | Password-hash algorithm |
| 40 | 4 | PBKDF2 iteration count |
| 44 | 16 | Random salt |
| 60 | 32 | Password hash |

Role 1 is admin and role 2 is viewer. Flag bit 0 means enabled; other bits are
zero. Hash algorithm 1 is PBKDF2-HMAC-SHA256 with the stored iteration count,
16-byte salt, and 32-byte output. The production default iteration count must be
benchmarked on both gateway targets before password creation is enabled. The
codec accepts 10,000 through 2,000,000 iterations and never silently
downgrades a record.

Usernames are lowercase ASCII letters, digits, `.`, `_`, or `-`. Password input
is UTF-8 between 8 and 128 bytes and is never persisted or logged. Mutations
cannot remove, disable, or demote the last enabled admin. An empty user set is
valid only for the physical initial-setup flow.

### API-token record

Exact size: 80 bytes.

| Relative offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | Token ID |
| 4 | 1 | Display-name byte length, `1..32` |
| 5 | 32 | Display-name UTF-8 bytes, zero-padded |
| 37 | 1 | Flags |
| 38 | 2 | Scope bitmask |
| 40 | 8 | Creation time, UTC Unix milliseconds or zero |
| 48 | 32 | SHA-256 of the raw token |

Flag bit 0 means enabled. Scope bits 0, 1, and 2 mean `gateway:read`,
`registry:read`, and `telemetry:read`; all other flag and scope bits are zero.
The raw token is 32 cryptographically random bytes shown once as unpadded
base64url. Only its digest is stored and verification uses constant-time
comparison.

`last_used_at` is deliberately runtime-only because persisting it on requests
would cause high-frequency flash wear. Login sessions, failed-login counters,
rate-limit state, and CSRF material are runtime state too.

## Installation secrets snapshot

NVS namespace: `gateway-secrets`; slot keys: `secret_a`, `secret_b`; magic:
`RSGS`; exact schema-1 size: 84 bytes.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 12 | Common snapshot header |
| 12 | 2 | Presence flags |
| 14 | 1 | Operational RFM69 network ID |
| 15 | 1 | Commissioning RFM69 network ID |
| 16 | 16 | Installation AES key |
| 32 | 16 | Commissioning AES key |
| 48 | 32 | Gateway device secret |
| 80 | 4 | CRC32 |

Presence bits 0, 1, and 2 correspond to installation key, commissioning key,
and device secret. Other bits are zero. Bytes for an absent value are zero. AES
keys are raw and may contain zeros. Operational and commissioning network IDs
must differ when both profiles are configured.

During initial installation, the operational network ID is generated randomly
from `1..255` and presented as an editable advanced value before confirmation.
The confirmed ID and installation key are committed together in this snapshot.
Ordinary edits are rejected after the registry contains an active node; later
changes require a staged migration protocol rather than an immediate update.

When the installation profile is absent, its network ID and key bytes are zero;
when present, its network ID is nonzero. When the commissioning profile is
absent, its network ID and key bytes are zero. A present commissioning network
ID may be zero, as in the current commissioning profile.

The device secret comes from the ESP32 hardware RNG, is not the public stable
gateway ID, survives ordinary settings/auth/network reset, and is reserved for
local secret derivation and authenticated export.

CRC32 provides neither confidentiality nor authenticity. Without ESP32 NVS or
flash encryption, physical flash access can recover keys and authentication
material. Enabling flash encryption and secure boot, or accepting this physical
attack, remains a production threat-model decision.

An empty secrets namespace is initialized only with a hardware-random device
secret. Initial setup generates and durably stores the operational network ID
and installation key; radio keys are never imported from build-time headers.
Existing but invalid slot data is reported as corruption and is never treated
as an empty store or automatically overwritten.

The schema-1 commissioning-key field is reserved and remains absent in the
production flow. Every node has a unique factory commissioning key supplied
with its UID. The user currently enters both values manually; future QR
scanning may populate the same request. The key is held only in RAM for one
pairing transaction and is never added to this snapshot or the registry.
Commissioning network ID is zero. After durable `JOIN_COMPLETE`, explicit
close, or timeout, the gateway wipes the temporary key.

## Node registry snapshot

NVS namespace: `node-reg`; slot keys: `registry_a`, `registry_b`; magic: `RSNR`; schema version 1; maximum size 1360 bytes. The registry has a fixed capacity of 64 records and does not allocate dynamically.

| ID | Use |
| ---: | --- |
| 0 | Unprovisioned node during commissioning |
| 1..99 | Persistent node IDs, lowest unused ID allocated first |
| 100 | Gateway |
| 101..254 | Reserved |
| 255 | Broadcast |

Pending, active, and disabled records retain their ID. Only explicit removal releases it.

Each stored record is 21 bytes:

| Relative offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 10 | Fixed tinyAVR factory UID |
| 10 | 1 | Node ID |
| 11 | 2 | Profile ID, little-endian; zero invalid |
| 13 | 3 | Firmware major, minor, patch |
| 16 | 1 | State: 1 pending, 2 active, 3 disabled |
| 17 | 4 | Latest commissioning request nonce, little-endian |

Runtime last-seen time, RSSI, telemetry, and counters are not persisted.

| Snapshot offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `RSNR` |
| 4 | 2 | Schema version |
| 6 | 4 | Generation |
| 10 | 1 | Record count |
| 11 | 1 | Reserved, zero |
| 12 | `count * 21` | Records |
| end | 4 | CRC32 over preceding bytes |

The state flow is `absent -> pending -> active`; explicit management may set `disabled` or remove a record. A repeated request with the same UID and profile retains the node ID and updates pending firmware/nonce. A different profile conflicts. Active or disabled records cannot be replaced through pairing. Matching repeated confirmation for an active record resends `JOIN_COMPLETE` without a write.

NVS work never runs in the radio-owner task. Commissioning persists a reservation before queuing `JOIN_ACCEPT`; synchronized store APIs own all registry access.

## Atomic operations and reset boundaries

There is no transaction across namespaces. Settings touch only
`gateway-config`; users/tokens touch only `gateway-auth`; commissioning touches
only `node-reg`; installation-key rotation touches only `gateway-secrets` and
needs a separate recovery workflow.

- Settings reset restores `gateway-config` defaults.
- Authentication reset requires a physical setup action and clears auth records
  plus runtime sessions.
- Registry reset does not silently erase installation secrets.
- Full installation reset clears all stores only after separate destructive
  confirmation.
- Ordinary reboot writes none of these stores.

## Backup mapping

Diagnostic export may contain decoded settings, public identity, non-secret
registry metadata, and diagnostics. It excludes password hashes, token hashes,
radio keys, and device secret.

An encrypted migration backup includes logical content from every persistent
store, including installation secrets, but not unused padding or slot CRCs. Its
KDF and authenticated-encryption container are specified separately. Restore
validates all limits and relationships before committing anything and preserves
the previous stores if validation fails.
