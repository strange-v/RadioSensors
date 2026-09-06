# Gateway REST API

Base path: `/api/v1`. JSON responses use `Content-Type: application/json`. Unless noted otherwise, endpoints are planned rather than implemented.

## Authentication

The initial setup endpoints are unauthenticated but require a time-bounded physical window opened with the gateway button. Management endpoints require an authenticated browser session and admin role where stated. Home Assistant uses a bearer API token with explicit read-only scopes.

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
| GET | `/api/v1/info` | none | planned |
| GET | `/api/v1/nodes` | session or `registry:read` | planned |
| GET, PUT | `/api/v1/settings` | session; admin for PUT | planned |
| GET, POST, PUT, DELETE | `/api/v1/users` | admin | planned |
| GET, POST, DELETE | `/api/v1/tokens` | admin | planned |
| POST | `/api/v1/pairing/open` | admin | planned |
| POST | `/api/v1/pairing/close` | admin | planned |
| POST | `/api/v1/commands` | admin | planned |
| GET | `/api/v1/export` | admin | planned |
| POST | `/api/v1/restore` | admin plus destructive confirmation | planned |
| GET | `/ws` | session or bearer token | provisional implementation |

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

Success: `201 {"status":"configured"}`. Errors: `400 invalid_request`, `403 physical_setup_required`, `409 setup_busy`, `409 setup_already_complete`, `422 invalid_setup_values`, `422 invalid_operational_network_id`, or `500` for hashing/storage failure.

Secrets and settings commit before authentication. The first enabled admin record is the final commit that changes the gateway to configured.

## Discovery and identity

Planned `GET /api/v1/info` is unauthenticated and returns API/stream versions, stable gateway ID, per-boot ID, hostname, board, firmware, registry generation, current UTC state, and capabilities. It never returns radio keys, factory UIDs, credentials, or tokens. mDNS advertises `_radiosensors._tcp`, REST major version, stable gateway ID, board, and firmware.

## Nodes

Planned `GET /api/v1/nodes` returns `registry_generation` and all relevant records, including nodes without telemetry since boot. Each node contains node ID, profile ID, firmware, registry state, optional user name, last-seen UTC, RSSI, telemetry presence, and later read-only command status. Factory UID is excluded from Home Assistant responses.

## Settings

Settings contain display name, mDNS enabled state, NTP enabled state, up to three NTP hosts, pairing-window seconds, and initial-setup-window seconds. NTP changes restart SNTP without reboot. Operational network ID is editable during initial setup but locked after an active node exists; later changes require a staged migration.

## Users and tokens

At most four local users are stored. Roles are `admin` and `viewer`; mutations cannot remove, disable, or demote the last enabled admin.

At most eight long-lived API tokens are stored. A new token contains 32 random bytes, is returned once as unpadded base64url, and is stored only as SHA-256. Initial scopes are `gateway:read`, `registry:read`, and `telemetry:read`. Runtime `last_used_at` is not persisted.

## WebSocket bootstrap

Home Assistant reads `/api/v1/info`, authenticates, reads `/api/v1/nodes`, then connects to `/ws` and waits for a complete snapshot. It refetches nodes when the stream registry generation differs or a `REGISTRY_CHANGED` event arrives. Binary frames are specified in [WEBSOCKET.md](WEBSOCKET.md).

## Security and backup

Plain HTTP bearer/session authentication does not prevent LAN traffic capture. TLS/WSS or a trusted-LAN requirement remains a production decision. Diagnostic export excludes password hashes, token hashes, radio keys, and device secrets. A complete migration backup must be encrypted and authenticated; its container is not yet specified.
