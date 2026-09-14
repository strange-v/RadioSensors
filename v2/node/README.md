# OSK Sense Node firmware

This PlatformIO project produces one statically composed ATtiny1614 image per stable telemetry profile. Shared code owns commissioning, radio, EEPROM, power, the wake clock, and command sessions; profiles own acquisition, report scheduling, and payload encoding.

## Builds

| Environment | Profile | Measurements | Tick |
| --- | ---: | --- | ---: |
| `climate_tmp112` | 2 | TMP112 temperature, supply voltage | 32 s |
| `climate_tmp112_debug` | 2 | same, with UART diagnostics | 32 s |
| `binary` | 5 | binary state, supply voltage | 250 ms |
| `binary_debug` | 5 | same, with UART diagnostics | 250 ms |
| `counter_reed` | 6 | pulse count, supply voltage | 250 ms |
| `counter_reed_debug` | 6 | same, with UART diagnostics | 250 ms |

Binary-input images with climate sensors (profiles 7, 8) are not implemented yet. An installation decides whether a binary input is a door, a window, or a float switch.

Build or upload one environment:

```powershell
pio run -e climate_tmp112
pio run -e climate_tmp112 -t upload
```

Hardware environments inherit serial UPDI on COM6 at 115200 baud. Adjust the local upload port in `platformio.ini` when necessary.

Upload never writes fuses. Write them once per chip before the first upload; every environment uses the same values (4 MHz from the 16 MHz oscillator, BOD 1.8 V in active mode only, EEPROM preserved on chip erase, UPDI pin kept):

```powershell
pio run -e climate_tmp112 -t fuses
```

## Runtime composition

Each environment compiles exactly one composition root from `src/` through `build_src_filter`. `NodeRuntime<Profile>` owns commissioning, the provisioning button, radio retry backoff, supply-voltage measurement, and sleep. The profile class in `include/Profiles/` owns acquisition, report scheduling, and payload encoding; the contract is documented in `NodeRuntime.h`. Profiles are template parameters, not virtual interfaces, because avr-gcc keeps vtables in RAM.

`NODE_TICK_MS` selects the RTC PIT wake period: 32 s for periodic images, 250 ms for polled inputs.

No input may float in sleep. Each composition root disables the digital input buffer of the pins its image leaves unconnected: the sensor I2C pads on counter and binary-input images, the reed pad on climate images, and PB2/PB3 outside debug builds. `NodeRadio` pulls up MISO, which the RFM69 releases while deselected.

Reported supply voltage is the lower of the measurement taken just before transmission and the one taken immediately after the previous transmission, so it reflects battery sag under radio load.

## Factory provisioning

Production firmware contains no shared commissioning key. Each ATtiny1614 must receive a unique 16-byte factory key in its 32-byte USERROW after the common firmware is flashed. Install the QR exporter once and provision a connected node through the same SerialUPDI adapter:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install -r scripts/requirements-provisioning.txt
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" scripts/provision_node.py --port COM6
```

The tool reads the 10-byte SIGROW UID, refuses to replace an existing valid record unless `--force` is supplied, generates the key with the operating system CSPRNG, writes and verifies USERROW, then exports a text credential, SVG QR, and `manifest.csv` under the git-ignored `provisioned_nodes/` directory. The QR payload is `web+opensmartkit:pair?v=1&family=sense&uid=<20 HEX>&key=<32 HEX>`. Treat every exported file as a secret manufacturing artifact and back it up outside the repository.

## Implemented runtimes

Every image supports per-node-key UID commissioning, recovery of provisional commissioning, dual-slot network configuration, Vcc measurement, acknowledged telemetry, bounded 1/5/15/60-minute radio retry, and RTC power-down scheduling independent of sleeping `millis()`. The climate image adds TMP112 one-shot measurement; the binary-input image adds the PA5 contact state; the counter image adds the PA5 pulse input and the wear-levelled counter journal.

The solar/supercapacitor climate policy schedules nominal 60 seconds above 2500 mV and 300 seconds at or below it. The 32-second RTC step yields about 64/320 seconds. Battery-powered climate builds use one compile-time interval and do not persist it.

`*_debug` environments log at 9600 baud on PB2. They preserve the RTC timebase but never call `sleep_cpu()`; idle iterations use a short delay. Do not use them to measure sleep current.

## Event-node policy

PA5 is the active-low reed/counter input. Each 250 ms tick reads it once, with the pull-up enabled only for that read. A read that disagrees with the confirmed state starts a debounce burst of five equal reads at 1 ms spacing in the same wake-up, so confirmation adds no tick of latency and costs power only on real changes. A LOW or HIGH phase longer than one tick is detected, except while a commissioning receive window or a held button blocks polling.

A counter additionally accepts a new level only after it has persisted for `NODE_COUNTER_MINIMUM_PHASE_MS` (500 ms) of RTC time, because a magnet moving slowly near the pull-in distance makes the reed chatter far longer than the debounce burst. With the 250 ms tick, phases of at least 750 ms are always accepted and phases shorter than 500 ms never are. Binary inputs do not use this filter.

A binary input reports each confirmed change in the same wake-up, even while radio retry backoff is active; if that attempt fails, the report waits for the backoff like any other. State `1` means the contact is open. Binary-input and counter profiles use a rolling one-hour keep-alive from the last acknowledged report. A counter counts from boot, including before commissioning, persists every confirmed LOW-to-HIGH pulse before any transmission, and reports the absolute count no more than once per minute while dirty.

Measured consumption and the battery budget are in [POWER.md](POWER.md).

PA6 is the active-low provisioning button. A hold is timed with `millis()` while the MCU stays awake, so input polling pauses until the button is released or the hold completes.

| Gesture | Effect |
| --- | --- |
| Released within 10 s | Unconfigured or provisional node: commissioning attempt. Active node: command session. |
| Held for 10 s | Invalidates both network configuration slots and restarts the node unconfigured. USERROW and profile EEPROM, including the counter, are preserved. Refused when the factory credentials are invalid, because the node could never rejoin. |

The gateway rejects a join request from a UID it still holds as active. To pair a reset node again, also delete it on the gateway (`DELETE /ui/nodes`).

## Radio power

Each image has a transmit power ceiling for its board and supply, `NODE_RADIO_MAX_POWER_LEVEL`; every image uses 2 until its board is measured. The node sends the ceiling in Join request and reports its level, fallback flag, and the RSSI of the last acknowledgement in every telemetry frame. That RSSI is sampled when the acknowledgement's sync word matches: the RFM69 keeps measuring the channel after a frame ends, so a read after reception returns anything between the frame and the noise floor ([PROTOCOL.md](../protocol/PROTOCOL.md#radio-power)).

| Event | Level |
| --- | --- |
| Commissioning | Ceiling |
| Acknowledgement carrying a power target | The target clamped to the ceiling, stored and applied at once; fallback cleared |
| Third consecutive report without acknowledgement | Ceiling with the fallback flag, unless already there |
| Boot with a stored level above the ceiling | Ceiling |

Level and fallback flag are part of the network configuration, so they survive a reset. The supply-limited flag is always zero.

## Command sessions

An active node opens a command session when its button is short-pressed, or when a telemetry acknowledgement carries the command-pending flag ([PROTOCOL.md](../protocol/PROTOCOL.md)). It sends Command ready, listens 250 ms for the answer, and repeats up to three times with the same nonce. Input polling pauses meanwhile, as during a commissioning window.

| Command | Handled by | Effect |
| --- | --- | --- |
| `set_count` | counter image | Pending result, ring write, Applied result ([EEPROM.md](EEPROM.md)); the new count is reported immediately |

Sessions started by the flag are limited to one per five minutes, and after one the gateway did not answer, to the 1/5/15/60-minute telemetry retry delays. A button press always opens a session. An event node reports at least hourly, so the button is the prompt way to reach one.

## Persistence and protocols

Network configuration uses two CRC-protected generation slots. Counter state uses a separate wear-levelled journal, and accepted `SET_COUNT` results have recoverable slots. Exact layouts are in [EEPROM.md](EEPROM.md).

Radio frame and telemetry payload bytes are defined only in [../protocol/PROTOCOL.md](../protocol/PROTOCOL.md). Profile IDs describe measurements, not installation labels.

## Native tests

```powershell
wsl bash v2/node/scripts/run_native_tests_wsl.sh
```

Remaining implementation work is tracked in [../../ROADMAP.md](../../ROADMAP.md).
