# Node firmware handoff

Last synchronized: 2026-09-05.

## Current implementation

`v2/node` is the production-oriented ATtiny1614 project. `v2/node_test` remains
board bring-up code and is not an architectural reference.

The completed vertical slice is `climate_tmp112` (profile 2):

- compile-time composition from a small `src/main.cpp`;
- UID-based commissioning and recovery of provisional commissioning;
- dual-slot, CRC-protected network configuration in EEPROM;
- TMP112 one-shot measurement, Vcc measurement, fixed telemetry encoding;
- acknowledged RFM69 transmission with 1/5/15/60-minute retry backoff;
- RTC-based power-down scheduling that does not depend on `millis()` while
  asleep.

The current build uses 10,734/16,384 bytes Flash (65.5%) and 470/2,048 bytes
RAM (22.9%). The configured climate interval is a provisional 15 minutes.

EEPROM support is also implemented for the future counter profile: a 32-entry
wear-levelled absolute-count ring and two recoverable `SET_COUNT` result slots.
See `EEPROM.md` for the exact byte layout.

Pure scheduling components are implemented and tested for future event nodes:

- plain door: immediate report on confirmed change;
- plain door/counter: rolling one-hour keep-alive from the last acknowledged
  telemetry report;
- counter: persist every pulse immediately, aggregate radio reports to no more
  than one per minute while the count changes;
- unsuccessful radio traffic does not move the keep-alive deadline.

Only `climate_tmp112` is in `default_envs`. The `door`, `counter_reed`,
`door_sht40`, and `door_tmp112` environments declare the required final images,
but their application runtimes are not connected yet.

## Verified commands

From the repository root:

```powershell
wsl bash v2/node/scripts/run_native_tests_wsl.sh
```

Result: 14 tests passed. They cover EEPROM corruption fallback, generation and
clock wrap, interrupted records, factory-reset isolation, rolling keep-alive,
one-minute counter aggregation, and radio retry backoff.

```powershell
C:\Users\stran\.platformio\penv\Scripts\platformio.exe run -d v2/node -e climate_tmp112
```

Result: successful ATtiny1614 release build. The absolute executable path is a
local-machine example only; do not put it into scripts.

The shared protocol/registry runner also passed 29 tests:

```powershell
wsl bash v2/protocol/scripts/run_native_tests_wsl.sh
```

## Resume here

Implement the plain `door` runtime first, then derive `counter_reed` from the
same event loop:

1. Generalize `LowPowerClock` so event profiles use a 250 ms RTC PIT while the
   climate profile retains its lower wake frequency.
2. Connect `PolledReedInput` on PA5 to `DoorReportSchedule`; initialize the
   state without emitting a false edge, send changes immediately, and reset the
   one-hour deadline only after ACK.
3. Connect `CounterStore` and `CounterReportSchedule`; count only confirmed
   LOW-to-HIGH transitions, persist before considering telemetry, and send the
   absolute count at most once per minute while dirty.
4. Add native state-machine tests before enabling `door` and `counter_reed` in
   `default_envs`, then build and record Flash/RAM for each environment.
5. Add the SHT40 and TMP112 door variants after the common event loop works.

After those profiles, freeze the generic `COMMAND` and `COMMAND_RESULT` byte
layouts and implement the PA6 interaction: short press opens a bounded
`COMMAND_READY` session; a press of at least 10 seconds performs network factory
reset while preserving counter state. `COMMAND_READY` and `NO_COMMAND` codecs
already exist, but there is no button or command runtime yet.

## Deliberately unresolved or provisional

- Exact climate reporting interval; 15 minutes is only the current build value.
- RTC wake strategy and measured sleep current on real hardware.
- PA5 sleep configuration current and real meter LOW/HIGH pulse widths.
- Command receive-window duration and command/result payload layouts.
- Commissioning retry/RX-window values after hardware measurement.
- Random jitter for future deadlines is designed but not implemented.
- `NODE_COMMISSIONING_KEY` in `platformio.ini` is a development placeholder;
  production key injection and the final shared-key versus device-secret model
  remain open.
- No production node image has yet been flashed and power-profiled on hardware.

