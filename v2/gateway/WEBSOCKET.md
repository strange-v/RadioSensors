# Gateway telemetry WebSocket

`ws://<gateway>/ws` exposes accepted node telemetry as binary messages. The
gateway does not decode profile-specific telemetry. All multi-byte integers are
little-endian.

## Common prefix

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | stream version, currently `1` |
| 1 | 1 | message kind |
| 2 | 4 | gateway telemetry sequence |

Message kinds are `1` (`SNAPSHOT_BEGIN`), `2` (`TELEMETRY`), and `3`
(`SNAPSHOT_END`). Begin and end messages are exactly six bytes. Their sequence
is the cache watermark observed while producing the snapshot.

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

On connect, the gateway sends `SNAPSHOT_BEGIN`, at most one cached telemetry
message per node, and `SNAPSHOT_END`. New telemetry is then pushed live. A
client must replace cached values by node ID and sequence, reconnect after a
disconnect, and rebuild its state from the next snapshot.

The socket sends a keep-alive ping every 30 seconds. At most four clients are
retained. A client whose bounded transmit queue fills is disconnected so it can
reconnect and resynchronize rather than silently remain stale.

The gateway starts SNTP after Ethernet obtains an address and stores only UTC.
A zero timestamp means the packet arrived before the gateway had synchronized;
the consumer should then use its own receipt time. Gateway uptime is diagnostic
only and is not part of the telemetry stream.
