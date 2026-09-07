# Gateway REST API

Base path: `/api/v1`. JSON responses use `Content-Type: application/json`. Unless noted otherwise, endpoints are planned rather than implemented.

## Authentication

The initial setup endpoints are unauthenticated but require a time-bounded physical window opened with the gateway button. Management endpoints require an authenticated browser session and admin role where stated. Home Assistant uses a bearer API token with explicit read-only scopes.

Browser login creates an in-memory session and sets an HttpOnly,
`SameSite=Strict` cookie named `rs_session`. Sessions expire after 12 hours of
inactivity and do not survive reboot. State-changing session requests must also
send the `csrf_token` returned by the session API in `X-CSRF-Token`. Login is
intended only for the trusted local network and does not implement request-rate
limiting.

Common bearer header:

```http
Authorization: Bearer <token>
```

Common error body:

```json
{"error":"machine_readable_code"}
```

## Endpoint summary

| Method | Path | Authorization | Status |
| --- | --- | --- | --- |
| GET | `/health` | none | implemented |
| GET | `/api/v1/setup` | none | implemented |
| POST | `/api/v1/setup` | physical setup window | implemented |
| GET | `/api/v1/info` | none | implemented |
| POST, GET, DELETE | `/api/v1/session` | credentials/session | implemented |
| GET | `/api/v1/nodes` | session or bearer `registry:read` | implemented |
| PATCH, DELETE | `/api/v1/nodes` | admin session + CSRF | implemented |
| GET, PUT | `/api/v1/settings` | session; admin for PUT | implemented |
| GET, POST, PUT, DELETE | `/api/v1/users` | admin | implemented |
| GET, POST, DELETE | `/api/v1/tokens` | admin | implemented |
| POST | `/api/v1/radio/reset` | admin session + CSRF | implemented |
| POST | `/api/v1/pairing/open` | admin | implemented |
| POST | `/api/v1/pairing/close` | admin | implemented |
| POST | `/api/v1/commands` | admin | planned |
| GET | `/api/v1/export` | admin | planned |
| POST | `/api/v1/restore` | admin plus destructive confirmation | planned |
| GET | `/ws` | session or bearer `telemetry:read` | provisional implementation |

## Initial setup

### `GET /api/v1/setup`

Returns whether an administrator must be created and whether the physical window is open.

```json
{"setup_required":true,"physical_window_active":false,"remaining_seconds":0}
```

### `POST /api/v1/setup`

Allowed exactly while `setup_required` and `physical_window_active` are both true.

```json
{
  "username":"admin",
  "password":"strong-password",
  "display_name":"Main gateway",
  "operational_network_id":128
}
```

| Field | Required | Validation |
| --- | --- | --- |
| `username` | yes | 1..32 lowercase ASCII letters, digits, `.`, `_`, `-` |
| `password` | yes | 8..128 UTF-8 bytes |
| `display_name` | no | 0..48 valid UTF-8 bytes without control characters |
| `operational_network_id` | no | 1..255 and different from the commissioning network |

Success creates the first browser session and returns the same body and cookie
as login, with status `201`. Errors: `400 invalid_request`, `403
physical_setup_required`, `409 setup_busy`, `409 setup_already_complete`, `422
invalid_setup_values`, `422 invalid_operational_network_id`, or `500` for
hashing/storage failure.

Secrets and settings commit before authentication. The first enabled admin record is the final commit that changes the gateway to configured.

## Browser session

### `POST /api/v1/session`

```json
{"username":"admin","password":"strong-password"}
```

On success it returns:

```json
{"user":{"id":1,"username":"admin","role":"admin"},"csrf_token":"32 lowercase hex characters"}
```

The response also sets the 64-hex-character opaque session cookie. Invalid
credentials return `401 invalid_credentials`; concurrent password verification
returns `409 login_busy`. Error responses do not reveal whether a username
exists.

### `GET /api/v1/session`

Returns the current user and CSRF token using the same response shape. Missing,
expired, or invalid cookies return `401 authentication_required`.

### `DELETE /api/v1/session`

Requires the session cookie and matching `X-CSRF-Token`, removes the RAM session,
expires the cookie, and returns `204`.

## Discovery, identity, and compatibility

`GET /api/v1/info` is unauthenticated. It returns the stable installation
identity, current boot identity, firmware and client-contract versions,
configured display name, Web UI state and version, board, and hostname:

```json
{"firmware_version":"0.8.0","api_version":1,"gateway_id":"cccd7e8a5e2bd5d8b9cb754240a82fd8","boot_id":"1d52f8108f3098bdcc0e1ac5fc71be4b","display_name":"Main gateway","ui":{"state":"ready","version":"0.1.0","required_api_version":1},"board":"Waveshare ESP32-S3-ETH + PoE","hostname":"rf-gateway-a085e3e6cc20"}
```

`gateway_id` is 128 bits encoded as 32 lowercase hexadecimal characters. It is
derived from the persistent installation device secret with domain-separated
SHA-256, remains stable across ordinary firmware updates and reboots, and
changes after the installation secrets are erased or replaced. `boot_id` has
the same encoding but is generated randomly on every boot. Consumers use a
changed `boot_id` to detect lost in-memory state and resynchronize.

`display_name` is the current value of the settings field of the same name and may be empty. Web UI and Home Assistant integration releases have their own SemVer versions because they are installed independently. Both declare the integer `api_version` they support. This contract number changes only for an incompatible external REST or WebSocket change; it is separate from the radio protocol version. The gateway serves the LittleFS UI only when its required API version exactly matches.

Future additions include stream versions, registry generation, current UTC state,
and capabilities. The info endpoint never returns radio keys, node UIDs,
credentials, or tokens. `_radiosensors._tcp` mDNS discovery advertises port 80
and TXT keys `api` (legacy alias), `api_version`, `gateway_id`, `boot_id`,
`firmware`, `board`, and `hostname`.

## Nodes

`GET /api/v1/nodes` returns `registry_generation` and all relevant records,
including nodes without telemetry since boot. Each node contains node ID,
the immutable uppercase factory `device_uid`, UTF-8 `display_name`, profile ID,
firmware, registry state and telemetry presence. When telemetry is available it
also includes last-seen UTC and RSSI. The UID is used with `gateway_id` as the
stable Home Assistant identity; the reusable radio `node_id` is not.

`PATCH /api/v1/nodes` renames a node and requires an admin session plus CSRF:

```json
{"node_id":7,"display_name":"Датчик у спальні"}
```

The name may be empty and must contain at most 48 valid UTF-8 bytes without
control characters. The response contains the applied name and durable
`registry_generation`. Invalid names return `422 invalid_display_name` and an
unknown node returns `404 node_not_found`.

`DELETE /api/v1/nodes` accepts `{"node_id":7}`, requires an admin session plus
CSRF, removes the durable registry entry and its in-memory telemetry, and returns
`204`. It does not reset an offline physical node: that node will keep its old
network credentials and must be factory-reset before it can be paired again.

## Settings

Settings contain display name, mDNS enabled state, NTP enabled state, up to three NTP hosts, pairing-window seconds, and initial-setup-window seconds. NTP changes restart SNTP without reboot. Operational network ID is editable during initial setup but locked after an active node exists; later changes require a staged migration.

`PUT /api/v1/settings` replaces the complete settings value and requires CSRF.
The response is the applied value with its durable generation. mDNS and NTP
services observe that generation and reapply changes without reboot; pairing
and physical initial-setup windows read their configured durations when opened.

## Pairing

`POST /api/v1/pairing/open` accepts manually entered per-device credentials:

```json
{"device_uid":"102132435465768798A9","factory_key":"00112233445566778899AABBCCDDEEFF"}
```

The UID is exactly 10 bytes and the factory key exactly 16 bytes, both encoded
as hexadecimal. The gateway switches the radio to commissioning network `0`,
keeps the key only in RAM, and accepts a `JOIN_REQUEST` only for the supplied
UID. The key is wiped when pairing succeeds, is closed, or expires. It is never
written to settings, secrets, registry, diagnostics, or logs. `POST
/api/v1/pairing/close` ends the window early. Both endpoints require an admin
session and CSRF and return the current pairing state and remaining seconds.

## Radio network

`POST /api/v1/radio/reset` regenerates the installation key and the operational
network ID, and requires an admin session plus CSRF. The body is optional:

```json
{"operational_network_id":42}
```

A value from 1 to 255 is applied as given; omit the field to have the gateway
generate one. Zero or a non-integer returns `422 invalid_operational_network_id`.

The operation clears the node registry and cached telemetry before writing the
new secrets, because every registered node is bound to the previous network and
key. It answers `202` with the applied ID and the number of removed records,
then restarts: the radio reads its profile once at boot, so a running gateway
cannot switch networks in place.

```json
{"operational_network_id":42,"removed_nodes":6,"restarting":true}
```

The gateway's own `deviceSecret` is left untouched, so `gateway_id` and the Home
Assistant identity survive the reset. Browser sessions do not: they live in RAM
and every client must sign in again once the gateway is back. Physical nodes are
not reset remotely — each keeps its old credentials and must be factory-reset
before it can be paired again.

## Users and tokens

At most four local users are stored. Roles are `admin` and `viewer`; mutations cannot remove, disable, or demote the last enabled admin.

`GET /api/v1/users` lists users without password material:

```json
{"users":[{"id":1,"username":"admin","role":"admin","enabled":true}]}
```

`POST /api/v1/users` requires CSRF and accepts `username`, `password`, `role`,
and an optional `enabled` flag (default `true`). It returns the new user with
status `201`. `PUT /api/v1/users` replaces `username`, `role`, and `enabled` for
the supplied `id`; an optional `password` replaces the password. A successful
update revokes every session belonging to that user. `DELETE /api/v1/users`
requires CSRF and a JSON body such as `{"id":2}`, revokes that user's sessions,
and returns `204`.

Usernames use 1..32 lowercase ASCII letters, digits, `.`, `_`, or `-`.
Passwords use 8..128 UTF-8 bytes. User mutations may return `404
user_not_found`, `409 mutation_busy`, `409 username_already_exists`, `409
user_capacity_reached`, `409 last_admin_required`, `422 invalid_user_values`,
or `500` for hashing/storage failure.

At most eight long-lived API tokens are stored. A new token contains 32 random bytes, is returned once as unpadded base64url, and is stored only as SHA-256. Initial scopes are `gateway:read`, `registry:read`, and `telemetry:read`. Runtime `last_used_at` is not persisted.

`GET /api/v1/tokens` lists token metadata but never returns token hashes or the
original token. `POST /api/v1/tokens` requires CSRF and accepts a name plus a
non-empty array containing only the three scopes above:

```json
{"name":"Home Assistant","scopes":["gateway:read","registry:read","telemetry:read"]}
```

The `201` response contains the generated `token` exactly once. `DELETE
/api/v1/tokens` requires CSRF and a JSON body such as `{"id":1}`; it returns
`204`. Token mutations are serialized and return `409 mutation_busy` when
another management write is in progress.

## WebSocket bootstrap

Home Assistant reads `/api/v1/info`, authenticates, reads `/api/v1/nodes`, then connects to `/ws` and waits for a complete snapshot. It refetches nodes when the stream registry generation differs or a `REGISTRY_CHANGED` event arrives. Binary frames are specified in [WEBSOCKET.md](WEBSOCKET.md).

The WebSocket handshake accepts either the browser session cookie or a bearer
token carrying `telemetry:read`; otherwise it returns HTTP `401` before the
protocol upgrade.

## Security and backup

Plain HTTP bearer/session authentication does not prevent LAN traffic capture. TLS/WSS or a trusted-LAN requirement remains a production decision. Diagnostic export excludes password hashes, token hashes, radio keys, and device secrets. A complete migration backup must be encrypted and authenticated; its container is not yet specified.
