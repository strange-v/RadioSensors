# OSK Sense Node firmware

This PlatformIO project produces one statically composed ATtiny1614 image per stable telemetry profile. Shared code owns commissioning, radio, EEPROM, scheduling, power, and command sessions; profiles own sensor acquisition and payload encoding. `node_test` remains separate bench firmware.

## Builds

| Environment | Profile | Measurements | Runtime status |
| --- | ---: | --- | --- |
| `climate_tmp112` | 2 | TMP112 temperature, supply voltage | implemented |
| `climate_tmp112_debug` | 2 | same, with UART diagnostics | implemented |
| `door` | 5 | binary state, supply voltage | declared, runtime pending |
| `counter_reed` | 6 | pulse count, supply voltage | declared, runtime pending |
| `door_sht40` | 7 | binary state, temperature, humidity, supply voltage | declared, runtime pending |
| `door_tmp112` | 8 | binary state, temperature, supply voltage | declared, runtime pending |

Build or upload one environment:

```powershell
pio run -e climate_tmp112
pio run -e climate_tmp112 -t upload
```

Hardware environments inherit serial UPDI on COM6 at 115200 baud. Adjust the local upload port in `platformio.ini` when necessary.

## Factory provisioning

Production firmware contains no shared commissioning key. Each ATtiny1614 must
receive a unique 16-byte factory key in its 32-byte USERROW after the common
firmware is flashed. Install the QR exporter once and provision a connected
node through the same SerialUPDI adapter:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install -r scripts/requirements-provisioning.txt
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" scripts/provision_node.py --port COM6
```

The tool reads the 10-byte SIGROW UID, refuses to replace an existing valid
record unless `--force` is supplied, generates the key with the operating
system CSPRNG, writes and verifies USERROW, then exports a text credential, SVG
QR, and `manifest.csv` under the git-ignored `provisioned_nodes/` directory.
The QR payload is
`web+opensmartkit:pair?v=1&family=sense&uid=<20 HEX>&key=<32 HEX>`. Treat every exported file
as a secret manufacturing artifact and back it up outside the repository.

## Implemented climate runtime

The production climate image supports per-node-key UID commissioning, recovery of provisional commissioning, dual-slot network configuration, TMP112 one-shot measurement, Vcc measurement, acknowledged telemetry, bounded 1/5/15/60-minute radio retry, and RTC power-down scheduling independent of sleeping `millis()`.

The solar/supercapacitor policy schedules nominal 60 seconds above 2500 mV and 300 seconds at or below it. The 32-second RTC step yields about 64/320 seconds. Battery-powered climate builds use one compile-time interval and do not persist it.

`climate_tmp112_debug` logs at 9600 baud on PB2. It preserves the RTC timebase but never calls `sleep_cpu()`; idle iterations use a short delay. Do not use it to measure sleep current.

## Event-node policy

PA5 is the active-low reed/counter input. It is sampled every 250 ms and accepts meter LOW and HIGH phases of at least one second. A door sends confirmed changes immediately. Door and counter profiles use a rolling one-hour keep-alive from the last acknowledged report. A counter persists every confirmed LOW-to-HIGH pulse and reports the absolute count no more than once per minute while dirty.

PA6 is the active-low provisioning button. An unconfigured-node press triggers commissioning immediately. The planned configured behavior is a short `COMMAND_READY` session and a 10-second network reset that preserves counter state.

## Persistence and protocols

Network configuration uses two CRC-protected generation slots. Counter state uses a separate wear-levelled journal, and accepted `SET_COUNT` results have recoverable slots. Exact layouts are in [EEPROM.md](EEPROM.md).

Radio frame and telemetry payload bytes are defined only in [../protocol/PROTOCOL.md](../protocol/PROTOCOL.md). Profile IDs describe measurements, not installation labels.

## Native tests

```powershell
wsl bash v2/node/scripts/run_native_tests_wsl.sh
```

Remaining implementation work is tracked in [../../ROADMAP.md](../../ROADMAP.md).
