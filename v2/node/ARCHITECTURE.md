# Node firmware architecture

## Design choice

The project produces a separate image for each stable protocol profile while
sharing one node core and reusable feature/driver modules. Selection happens at
compile time; production firmware has no dynamic feature registry, heap-based
composition, RTTI, or virtual feature dispatch.

```text
main -> selected profile -> enabled features -> hardware drivers
  `-> NodeCore: commissioning, radio, persistence, commands, power
```

## Planned source boundaries

```text
src/main.cpp                  composition root only
lib/NodeCore/                 radio, commissioning, config, sleep, commands
lib/Features/                 battery, climate, binary input, pulse counter
lib/Drivers/                  TMP112, SHT40, GPIO/reed and MCU peripherals
lib/Telemetry/                fixed profile encoders
include/Profiles/             compile-time profile definitions
test/                         native state-machine and codec tests
```

A feature owns acquisition and state, but never its byte offset in a packet.
The selected profile owns telemetry encoding and the supported-command set.
Drivers do not know about radio, commissioning, profiles, or Home Assistant.

## Initial profiles

| ID | Contract |
| ---: | --- |
| 1 | supply voltage |
| 2 | temperature, supply voltage |
| 3 | temperature, humidity, supply voltage |
| 4 | temperature, humidity, pressure, supply voltage |
| 5 | binary state, supply voltage |
| 6 | cumulative pulse count, supply voltage; supports `SET_COUNT` |
| 7 | binary state, temperature, humidity, supply voltage |
| 8 | binary state, temperature, supply voltage |

IDs are opaque, sequential, stable, and never reused. Profile 0 is invalid.
Profile 8 is the TMP112 door-node contract; the removed counter-temperature
proposal is not part of the catalogue.

## Persistence domains

Network configuration uses two CRC-protected generation slots. Counter state
uses a separate wear-levelled journal and survives a network factory reset.
The accepted command ID and counter update must become durable atomically before
a `SET_COUNT` result is sent.

## Size and timing policy

Every environment is size-checked independently. The initial budgets are 85%
of 16 KiB Flash and 50% of 2 KiB SRAM. Production builds compile out serial
diagnostics and use integer/fixed-point sensor conversion. Execution time is
dominated by sensor conversion, EEPROM, and radio I/O; profile/feature dispatch
must resolve to direct compile-time calls.

## Input power policy

The reed/counter input is connected from PA5 to ground and is sampled every
250 ms. Firmware enables the internal pull-up only for the bounded debounce
sample, then drives the pin low (or disables its input buffer) during sleep.
This avoids continuous pull-up current while a reed remains closed. Counter
semantics match V1: increment once on a confirmed LOW-to-HIGH transition, the
end of a complete pulse.

The counter is persisted immediately after every confirmed pulse and before
radio transmission. Capacity planning uses 150,000 pulses/year so that winter
gas consumption is not underestimated; one pulse per second describes the
shortest expected pulse timing, not the sustained event rate. With the
proposed 32-entry wear-levelled count ring and 100,000-cycle EEPROM endurance,
the nominal minimum write budget is 3.2 million persisted pulses, or roughly
21.3 years at the planning rate. No batching or power-fail checkpoint is
required for the initial design.

The normally-open provisioning button on PA6 uses asynchronous wake with its
pull-up enabled. It only draws pull-up current while physically pressed, which
is acceptable for the short press and deliberate 10-second reset gesture.
For an unconfigured node, a debounced press immediately triggers a join attempt
and bypasses the periodic join-retry deadline. Configured-node command sessions
and long-press handling are not connected yet.

## Reporting intervals

Door and counter profiles without a temperature/humidity sensor are primarily
event-driven. Their keep-alive interval is one hour, measured from the last
successfully acknowledged telemetry transmission rather than from a fixed wall
clock. For example, an acknowledged door event at minute 55 moves the next
keep-alive to minute 115. A failed transmission does not move the deadline.
Radio failures are retried with bounded backoff of 1, 5, 15, then 60 minutes;
the one-hour delay is retained until a transmission succeeds.

A plain door profile reports every confirmed state change immediately. A
counter persists every confirmed pulse immediately, but coalesces radio
traffic: after at least one new pulse it reports the current absolute count no
more often than once per minute. If there are no reportable events, both
profiles still send their current state/count and supply voltage after one
hour. A small randomized jitter may be added when arming a future deadline,
without changing these nominal intervals.

Profiles that include TMP112 or SHT40 use the climate reporting policy instead.
Solar/supercapacitor builds use the V1 threshold semantics: after a successful
report, the next report is scheduled in 60 seconds when VCC is above 2500 mV,
or in 300 seconds when VCC is at or below 2500 mV. Battery-powered builds use
one compile-time interval selected by their build environment. It is deliberately
not a runtime/EEPROM setting until field use demonstrates a need to reconfigure
sealed nodes without reflashing them.

## Command sessions

Application commands use an explicit pull model. Normal telemetry waits only
for its radio acknowledgement. A short PA6 press causes the node to send
`COMMAND_READY` with a fresh session nonce and listen for a bounded response.
The gateway replies with its one durable pending command or `NO_COMMAND`.

The node persists the applied value and command ID before returning
`COMMAND_RESULT`. If that result is lost, a future button session receives the
same command ID; the node resends its stored result without reapplying the
command. One command is delivered per button session. A press held for at least
10 seconds performs network factory reset instead and preserves counter state.

## Hardware decisions still required

- measured current comparison of the selected PA5 sleep configuration;

The accepted meter input contract is a minimum LOW width of one second and a
minimum HIGH width of one second. The 250 ms polling period provides four
sampling opportunities per minimum-width phase before the bounded debounce
sample.

## Implementation status

The `climate_tmp112` vertical slice is implemented and build-verified. Event
schedulers and counter persistence are implemented as host-tested components,
but the door/counter application loops, PA6 command session, and long-press
factory reset are not connected yet. Continue from `HANDOFF.md`; do not infer
readiness merely from the presence of a PlatformIO environment.
