# Gateway REST API

JSON responses use `Content-Type: application/json`. Unless noted otherwise, endpoints are planned rather than implemented.

There are two API surfaces, split by how fast they may change rather than by who calls them:

| Prefix | Contents | Stability |
| --- | --- | --- |
| `/api` | the external client contract | changes only with an `api_version` bump |
| `/ui` | everything the Web UI needs | may change whenever the firmware does |

`/health` and `/ws` sit outside both: the first is an unversioned liveness probe, the second versions itself through `stream_version`.

## Versions

One number per wire contract, and nothing else is a version. They move independently.

| Number | Governs | Checked by |
| --- | --- | --- |
| `api_version` | REST under `/api` | the client, from `/api/info` |
| `stream_version` | WebSocket frame format | the client, from `/api/info` |
| `protocol_major` | radio frames, see [PROTOCOL.md](../protocol/PROTOCOL.md) | nodes |

A bump means a breaking change; additive changes never bump. A consumer therefore declares the **set** of versions it supports and checks membership, not a minimum.

URL prefixes carry no version: the gateway serves one version at a time, and `/api/info` must stay reachable across bumps. `/ui` has no version at all — the Web UI checks `required_firmware` instead.

## Authentication

The initial setup endpoints are unauthenticated but require a time-bounded physical window opened with the gateway button. Management endpoints require an authenticated browser session and admin role where stated. Home Assistant uses a bearer API token with explicit read-only scopes.

Browser login creates an in-memory session and sets an HttpOnly, `SameSite=Strict` cookie named `rs_session`. Sessions expire after 12 hours of inactivity and do not survive reboot. State-changing session requests must also send the `csrf_token` returned by the session API in `X-CSRF-Token`. Login is intended only for the trusted local network and does not implement request-rate limiting.

Common bearer header:

```http
Authorization: Bearer <token>
```

Common error body:

```json
{"error":"machine_readable_code"}
```

## Endpoint summary

Client contract:

| Method | Path | Authorization | Status |
| --- | --- | --- | --- |
| GET | `/api/info` | none | implemented |
| GET | `/api/nodes` | session or bearer `telemetry:read` | implemented |
| GET | `/ws` | session or bearer `telemetry:read` | provisional implementation |

Web UI:

| Method | Path | Authorization | Status |
| --- | --- | --- | --- |
| GET | `/ui/setup` | none | implemented |
| POST | `/ui/setup` | physical setup window | implemented |
| POST, GET, DELETE | `/ui/session` | credentials/session | implemented |
| GET | `/ui/status` | session | implemented |
| GET | `/ui/clients` | session | implemented |
| GET | `/ui/telemetry/last` | session | implemented |
| PATCH, DELETE | `/ui/nodes` | admin session + CSRF | implemented |
| GET, PUT | `/ui/settings` | session; admin for PUT | implemented |
| GET, POST, PUT, DELETE | `/ui/users` | admin | implemented |
| GET, POST, DELETE | `/ui/tokens` | admin | implemented |
| POST | `/ui/radio/reset` | admin session + CSRF | implemented |
| POST | `/ui/pairing/open` | admin | implemented |
| POST | `/ui/pairing/close` | admin | implemented |
| POST | `/ui/commands` | admin | planned |
| GET | `/ui/export` | admin | planned |
| POST | `/ui/restore` | admin plus destructive confirmation | planned |

Neither:

| Method | Path | Authorization | Status |
| --- | --- | --- | --- |
| GET | `/health` | none | implemented |

## External client contract

An external client — the Home Assistant integration, or anything else that reads this gateway over the network — is released and updated independently of the firmware. It may therefore depend only on the three endpoints below, and `api_version` describes that set and nothing else.

| Purpose | Endpoint |
| --- | --- |
| Identity and compatibility | `GET /api/info` |
| Node registry, with its generation | `GET /api/nodes` |
| Telemetry stream | `GET /ws` |

A client is given its bearer token by a person, who creates it with `POST /ui/tokens` from the Web UI. Everything under `/ui` is internal and a client must not call it, including that one.

The Web UI reads `/api/nodes` too. The registry read is deliberately not duplicated, so the endpoint an integration depends on is the one exercised on every dashboard refresh.

A consumer must ignore fields it does not recognize. Adding a field therefore never bumps `api_version`; removing, renaming, or retyping one does.

`/api/nodes` returns registry records and nothing else. When the Web UI needs something that shape does not carry — pending command state, for instance — it gets its own resource under `/ui` rather than widening the contract.

## Initial setup

### `GET /ui/setup`

Returns whether an administrator must be created and whether the physical window is open.

```json
{"setup_required":true,"physical_window_active":false,"remaining_seconds":0}
```

### `POST /ui/setup`

Allowed exactly while `setup_required` and `physical_window_active` are both true.

```json
{
  "username":"admin",
  "password":"strong-password",
  "hostname":"osk-hub-floor1",
  "operational_network_id":128
}
```

| Field | Required | Validation |
| --- | --- | --- |
| `username` | yes | 1..32 lowercase ASCII letters, digits, `.`, `_`, `-` |
| `password` | yes | 8..128 UTF-8 bytes |
| `hostname` | no | 0..32 DNS label characters; empty keeps `osk-hub-<mac>` |
| `operational_network_id` | no | 1..255 and different from the commissioning network |

Success creates the first browser session and returns the same body and cookie as login, with status `201`. Errors: `400 invalid_request`, `403 physical_setup_required`, `409 setup_busy`, `409 setup_already_complete`, `422 invalid_setup_values`, `422 invalid_operational_network_id`, or `500` for hashing/storage failure.

Secrets and settings commit before authentication. The first enabled admin record is the final commit that changes the gateway to configured.

## Browser session

### `POST /ui/session`

```json
{"username":"admin","password":"strong-password"}
```

On success it returns:

```json
{"user":{"id":1,"username":"admin","role":"admin"},"csrf_token":"32 lowercase hex characters"}
```

The response also sets the 64-hex-character opaque session cookie. Invalid credentials return `401 invalid_credentials`; concurrent password verification returns `409 login_busy`. Error responses do not reveal whether a username exists.

### `GET /ui/session`

Returns the current user and CSRF token using the same response shape. Missing, expired, or invalid cookies return `401 authentication_required`.

### `DELETE /ui/session`

Requires the session cookie and matching `X-CSRF-Token`, removes the RAM session, expires the cookie, and returns `204`.

## Discovery, identity, and compatibility

`GET /api/info` is unauthenticated. It returns the stable installation identity, current boot identity, firmware and client-contract versions, configured display name, Web UI state and version, board, and hostname:

```json
{"firmware_version":"0.8.0","api_version":1,"gateway_id":"cccd7e8a5e2bd5d8b9cb754240a82fd8","boot_id":"1d52f8108f3098bdcc0e1ac5fc71be4b","ui":{"state":"ready","version":"0.1.0","required_firmware":"0.8"},"board":"Waveshare ESP32-S3-ETH + PoE","hostname":"osk-hub-a085e3e6cc20"}
```

`gateway_id` is 128 bits encoded as 32 lowercase hexadecimal characters. It is derived from the persistent installation device secret with domain-separated SHA-256, remains stable across ordinary firmware updates and reboots, and changes after the installation secrets are erased or replaced. `boot_id` has the same encoding but is generated randomly on every boot. Consumers use a changed `boot_id` to detect lost in-memory state and resynchronize.

`hostname` is the name the gateway answers to over mDNS and DHCP. The Web UI and the Home Assistant integration have their own SemVer versions because they are installed independently of the firmware and of each other.

They do not gate on the same thing, because they do not depend on the same thing. The integration declares which `api_version` values it supports and checks `/api/info`. The Web UI declares the firmware series it was built for in `required_firmware`, and the gateway serves the LittleFS image only when the `major.minor` matches; the patch level is ignored. That is the honest test: what the UI depends on is `/ui/*`, which moves with the firmware, not the client contract.

The info endpoint never returns radio keys, node UIDs, credentials, or tokens. `_osk-sense._tcp` mDNS discovery advertises port 80 and TXT keys `api` (legacy alias), `api_version`, `stream_version`, `gateway_id`, `boot_id`, `firmware`, `board`, and `hostname`. `stream_version` matches the first byte of every binary WebSocket message.

## Nodes

`GET /api/nodes` returns `registry_generation` and all relevant records, including nodes without telemetry since boot. Each node contains node ID, the immutable uppercase factory `device_uid`, UTF-8 `display_name`, profile ID, firmware, registry state and telemetry presence. When telemetry is available it also includes last-seen UTC and RSSI. The UID is used with `gateway_id` as the stable Home Assistant identity; the reusable radio `node_id` is not.

`PATCH /ui/nodes` renames a node and requires an admin session plus CSRF:

```json
{"node_id":7,"display_name":"Датчик у спальні"}
```

The name may be empty and must contain at most 48 valid UTF-8 bytes without control characters. The response contains the applied name and durable `registry_generation`. Invalid names return `422 invalid_display_name` and an unknown node returns `404 node_not_found`.

`DELETE /ui/nodes` accepts `{"node_id":7}`, requires an admin session plus CSRF, removes the durable registry entry and its in-memory telemetry, and returns `204`. It does not reset an offline physical node: that node will keep its old network credentials and must be factory-reset before it can be paired again.

## Settings

Settings contain hostname, mDNS enabled state, NTP enabled state, up to three NTP hosts, pairing-window seconds, and initial-setup-window seconds. NTP changes restart SNTP without reboot. Operational network ID is editable during initial setup but locked after an active node exists; later changes require a staged migration.

`PUT /ui/settings` replaces the complete settings value and requires CSRF. The response is the applied value with its durable generation. mDNS and NTP services observe that generation and reapply changes without reboot; pairing and physical initial-setup windows read their configured durations when opened.

`hostname` is a DNS label: lowercase ASCII letters, digits and hyphens, never leading or trailing, at most 32 characters. Empty means the gateway uses `osk-hub-<mac>`, so a device is reachable before anyone has named it. Both the mDNS name and the DHCP hostname come from this value, and `ETH.setHostname()` only applies while Ethernet starts, so a change takes effect on the next restart rather than leaving the two disagreeing. A name that collides with another device on the network is resolved by mDNS appending a suffix, which means the gateway then answers to a name nobody chose.

## Pairing

`POST /ui/pairing/open` accepts manually entered per-device credentials:

```json
{"device_uid":"102132435465768798A9","factory_key":"00112233445566778899AABBCCDDEEFF"}
```

The UID is exactly 10 bytes and the factory key exactly 16 bytes, both encoded as hexadecimal. The gateway switches the radio to commissioning network `0`, keeps the key only in RAM, and accepts a `JOIN_REQUEST` only for the supplied UID. The key is wiped when pairing succeeds, is closed, or expires. It is never written to settings, secrets, registry, diagnostics, or logs. `POST /ui/pairing/close` ends the window early. Both endpoints require an admin session and CSRF and return the current pairing state and remaining seconds.

## Radio network

`POST /ui/radio/reset` regenerates the installation key and the operational network ID, and requires an admin session plus CSRF. The body is optional:

```json
{"operational_network_id":42}
```

A value from 1 to 255 is applied as given; omit the field to have the gateway generate one. Zero or a non-integer returns `422 invalid_operational_network_id`.

The operation clears the node registry and cached telemetry before writing the new secrets, because every registered node is bound to the previous network and key. It answers `202` with the applied ID and the number of removed records, then restarts: the radio reads its profile once at boot, so a running gateway cannot switch networks in place.

```json
{"operational_network_id":42,"removed_nodes":6,"restarting":true}
```

The gateway's own `deviceSecret` is left untouched, so `gateway_id` and the Home Assistant identity survive the reset. Browser sessions do not: they live in RAM and every client must sign in again once the gateway is back. Physical nodes are not reset remotely — each keeps its old credentials and must be factory-reset before it can be paired again.

## Users and tokens

At most four local users are stored. Roles are `admin` and `viewer`; mutations cannot remove, disable, or demote the last enabled admin.

`GET /ui/users` lists users without password material:

```json
{"users":[{"id":1,"username":"admin","role":"admin","enabled":true}]}
```

`POST /ui/users` requires CSRF and accepts `username`, `password`, `role`, and an optional `enabled` flag (default `true`). It returns the new user with status `201`. `PUT /ui/users` replaces `username`, `role`, and `enabled` for the supplied `id`; an optional `password` replaces the password. A successful update revokes every session belonging to that user. `DELETE /ui/users` requires CSRF and a JSON body such as `{"id":2}`, revokes that user's sessions, and returns `204`.

Usernames use 1..32 lowercase ASCII letters, digits, `.`, `_`, or `-`. Passwords use 8..128 UTF-8 bytes. User mutations may return `404 user_not_found`, `409 mutation_busy`, `409 username_already_exists`, `409 user_capacity_reached`, `409 last_admin_required`, `422 invalid_user_values`, or `500` for hashing/storage failure.

At most eight long-lived API tokens are stored. A new token contains 32 random bytes, is returned once as unpadded base64url, and is stored only as SHA-256. Runtime `last_used_at` is not persisted.

There is exactly one scope, `telemetry:read`, and it covers both `GET /api/nodes` and `/ws`. The two are not separable in practice: the WebSocket bootstrap below needs the registry to resolve node IDs, so a token holding one without the other could authenticate for half of a job it cannot finish. `/api/info` is unauthenticated so a client can check compatibility before it has a credential, and is covered by no scope. Bearer tokens cannot write: every mutation requires an admin session plus CSRF, so a write scope would guard nothing today. The stored field stays a bitfield for the day that changes.

`GET /ui/tokens` lists token metadata but never returns token hashes or the original token. `POST /ui/tokens` requires CSRF and accepts a name:

```json
{"name":"Home Assistant"}
```

`scopes` is optional. Omitting it grants `telemetry:read`, which is what the Web UI sends. A supplied value must be a non-empty array containing only `telemetry:read`; anything else returns `422 invalid_token_values` rather than silently granting the scope that does exist.

Names are at most 32 UTF-8 bytes and need not be unique. The `201` response contains the generated `token` exactly once and omits `enabled`; a new token is always enabled. `DELETE /ui/tokens` requires CSRF and a JSON body such as `{"id":1}`; it returns `204`. Token mutations are serialized and return `409 mutation_busy` when another management write is in progress.

## WebSocket bootstrap

A client reads `/api/info`, checks `api_version` and `stream_version`, authenticates, reads `/api/nodes`, then connects to `/ws` and waits for a complete snapshot. Binary frames are specified in [WEBSOCKET.md](WEBSOCKET.md).

Keeping the registry fresh needs two rules and no polling:

- Refetch `/api/nodes` on a `REGISTRY_CHANGED` message, which carries the new `registry_generation`. This covers renames, deletions, completed pairings, and radio network resets that happen while the socket is up.
- Refetch `/api/info` and `/api/nodes` after every reconnect. A dropped socket is also how a client learns the gateway rebooted, so no separate reboot signal is needed; `boot_id` confirms it rather than carrying it.

`REGISTRY_CHANGED` is what makes radio `node_id` reuse safe: IDs are recycled, so without it telemetry for a reissued ID would be attributed to the previous `device_uid` until the consumer next polled. A slow poll remains a reasonable safety net, but it is not the mechanism.

The WebSocket handshake accepts either the browser session cookie or a bearer token carrying `telemetry:read`; otherwise it returns HTTP `401` before the protocol upgrade.

A client should identify itself with an `X-Client` handshake header naming the product and version, for example `home-assistant/0.3.0`. This is the only source the gateway has for who is on a socket: an API key's name is free text someone typed and need not be unique, so it says nothing about the client holding it. The gateway keeps at most 32 printable ASCII characters per client and reports them at `GET /ui/clients`:

```json
{"clients":[{"id":3,"name":"home-assistant/0.3.0"},{"id":4,"name":""}]}
```

An empty name is a client that sent no header; it is reported as unidentified rather than attributed to a key. The endpoint requires a browser session of any role and is not available to a bearer token.

## Liveness and status

`GET /health` is unauthenticated and deliberately almost empty:

```json
{"status":"ok","boot_id":"1d52f8108f3098bdcc0e1ac5fc71be4b"}
```

It exists so something on the network can tell the gateway is up without a credential, and so the Web UI can watch for a reboot after a radio network reset has killed every session. `boot_id` is the one detail worth publishing here: it changes on every boot, which is what distinguishes "it came back" from "it never went down", and mDNS broadcasts it anyway. Nothing else belongs in this response, and a client must not read it — see "External client contract".

`GET /ui/status` requires a session and returns everything the gateway knows about itself: ethernet, radio, storage, time, registry, telemetry, setup and pairing windows, WebSocket counters, OTA, Web UI state, uptime, free heap, reset reason, and the radio pinout and counters. Those are diagnostics for whoever runs the gateway, not facts for the network, which is why they are not in `/health`.

## Development diagnostics

`GET /ui/telemetry/last` returns the most recently accepted telemetry record with its payload as hexadecimal, for looking at frames on the bench without a WebSocket client. It requires a browser session and does not accept a bearer token: it returns decoded node telemetry, which is what `telemetry:read` protects, and it is not part of the external client contract. Before the first frame it answers `404 no_telemetry`.

```json
{"node_id":3,"profile_id":1,"received_at_ms":1770000000000,"rssi":-74,"size":3,"sequence":812,"payload_hex":"40e40c"}
```

## Security and backup

Plain HTTP bearer/session authentication does not prevent LAN traffic capture. TLS/WSS or a trusted-LAN requirement remains a production decision. Diagnostic export excludes password hashes, token hashes, radio keys, and device secrets. A complete migration backup must be encrypted and authenticated; its container is not yet specified.
