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

The current build uses 11,406/16,384 bytes Flash (69.6%) and 497/2,048 bytes
RAM (24.3%). The solar climate policy is 60 seconds above 2500 mV and 300
seconds at or below 2500 mV.

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

`climate_tmp112_debug` logs commissioning and telemetry at 9600 baud on PB2.
It keeps the RTC timebase but replaces `sleep_cpu()` with a short delay,
so it must not be used for sleep-current measurements. Its current build uses
12,621/16,384 bytes Flash (77.0%) and 497/2,048 bytes RAM (24.3%).

All hardware environments inherit the proven `v2/node_test` serial UPDI upload
configuration: COM6 at 115200 baud.

PA6 now uses a CHANGE interrupt to wake an unconfigured node. A debounced press
immediately starts a join attempt and bypasses the nominal five-minute retry
(about 320 seconds with the 32-second climate PIT). Configured-node short-press
command sessions and 10-second factory reset are not implemented yet.

## Verified commands

From the repository root:

```powershell
wsl bash v2/node/scripts/run_native_tests_wsl.sh
```

Result: 16 tests passed. They cover EEPROM corruption fallback, generation and
clock wrap, interrupted records, factory-reset isolation, rolling keep-alive,
one-minute counter aggregation, radio retry backoff, and fixed/adaptive climate
report policies.

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

During that command milestone, add the planned one-byte versioned
`COMMAND_PENDING` payload to telemetry ACKs. `NodeRadio::sendTelemetry()` must
preserve and validate the ACK payload before sleeping; a valid pending hint
starts the same nonce-bound `COMMAND_READY` pull session automatically. Do not
put the command itself in the ACK. Keep PA6 as the manual immediate trigger.

## Deliberately unresolved or provisional

- RTC wake strategy and measured sleep current on real hardware.
- PA5 sleep configuration current.

The meter input contract is now fixed at minimum one-second LOW and one-second
HIGH phases. A 250 ms poll period provides four opportunities to observe each
phase. Battery climate intervals remain build-time configurable rather than
runtime persisted settings.
- Command receive-window duration and command/result payload layouts.
- Commissioning retry/RX-window values after hardware measurement.
- Random jitter for future deadlines is designed but not implemented.
- `NODE_COMMISSIONING_KEY` in `platformio.ini` is a development placeholder;
  production key injection and the final shared-key versus device-secret model
  remain open.
- No production node image has yet been flashed and power-profiled on hardware.
