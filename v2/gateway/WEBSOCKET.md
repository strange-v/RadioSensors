# Gateway telemetry WebSocket

The layout below is provisional: the identity fields listed under "Common prefix" remain to be designed. `API.md` defines the lifecycle in "WebSocket bootstrap".

`ws://<gateway>/ws` exposes accepted node telemetry as binary messages. The gateway does not decode profile-specific telemetry. All multi-byte integers are little-endian.

## Common prefix

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | stream version, currently `1` |
| 1 | 1 | message kind |
| 2 | 4 | gateway telemetry sequence |

The same stream version is advertised as the `stream_version` TXT field of the gateway's `_osk-sense._tcp` mDNS service.

Message kinds are `1` (`SNAPSHOT_BEGIN`), `2` (`TELEMETRY`), `3` (`SNAPSHOT_END`), and `4` (`REGISTRY_CHANGED`). Begin and end messages are exactly six bytes. Their sequence is the cache watermark observed while producing the snapshot.

**A consumer must ignore a message kind it does not recognize.** Without that rule every later addition breaks every deployed client.

Before the stream is frozen it must also identify the stable gateway ID, per-boot ID, and sequence space. These may be carried by a new `HELLO` message and expanded snapshot control messages. Their exact byte layouts remain to be designed and covered by shared known vectors. A reboot needs no separate signal: it drops the socket, and reconnecting rebuilds state from the next snapshot.

## TELEMETRY

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 6 | common prefix |
| 6 | 1 | node ID |
| 7 | 2 | profile ID |
| 9 | 8 | UTC Unix milliseconds when received, or `0` before NTP sync |
| 17 | 2 | signed RSSI in dBm |
| 19 | 1 | radio payload size |
| 20 | variable | complete v2 radio frame, including its protocol header |

## REGISTRY_CHANGED

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 6 | common prefix |
| 6 | 4 | new durable registry generation |

Sent when the durable node registry changes while a client is connected: a rename, a deletion, or a completed pairing. The consumer refetches `GET /api/nodes` and compares `registry_generation`. A radio network reset also clears the registry, but it restarts the gateway, so that case resolves through reconnection whether or not the notification is delivered first.

Radio `node_id` values are reused: a node that is deleted and replaced can be issued the same ID, so without this message telemetry for that ID would be attributed to the previous `device_uid` until the consumer next polled.

The gateway compares the published generation once per main-loop pass, so the message follows the write by an iteration. A client that misses it is not left stale: a full transmit queue disconnects it, and reconnecting resynchronizes from the snapshot.

## Session

On connect, the gateway sends `SNAPSHOT_BEGIN`, at most one cached telemetry message per node, and `SNAPSHOT_END`. New telemetry is then pushed live. A client must replace cached values by node ID and sequence, reconnect after a disconnect, and rebuild its state from the next snapshot.

The socket sends a keep-alive ping every 30 seconds. At most four clients are retained. A client whose bounded transmit queue fills is disconnected so it can reconnect and resynchronize rather than silently remain stale.

The gateway starts SNTP after Ethernet obtains an address and stores only UTC. A zero timestamp means the packet arrived before the gateway had synchronized; the consumer should then use its own receipt time. Gateway uptime is diagnostic only and is not part of the telemetry stream.

The handshake requires either an authenticated browser session cookie or an API bearer token with the `telemetry:read` scope. Unauthorized clients receive HTTP `401` before the protocol upgrade.

A client should also send an `X-Client` handshake header naming itself, such as `home-assistant/0.3.0`, so the gateway can report who is connected. It is optional: a client that omits it streams normally and is listed as unidentified. See "WebSocket bootstrap" in `API.md`. When the planned `HELLO` message lands, this identity moves into it.
