# RadioSensors v2 node firmware

This PlatformIO project builds one statically composed firmware image per node
profile. It is intentionally separate from `node_test`, which remains board
bring-up and commissioning test code.

Declared build environments:

| Environment | Profile | Features |
| --- | ---: | --- |
| `climate_tmp112` | 2 | TMP112 temperature, supply voltage |
| `door` | 5 | Reed/binary state, supply voltage |
| `door_sht40` | 7 | Reed/binary state, SHT40 temperature and humidity, supply voltage |
| `door_tmp112` | 8 | Reed/binary state, TMP112 temperature, supply voltage |
| `counter_reed` | 6 | Persistent reed pulse counter, supply voltage |

The first completed vertical runtime slice is `climate_tmp112`: commissioning,
dual-slot EEPROM configuration, TMP112/Vcc telemetry, acknowledged radio
transmission, and RTC power-down scheduling. The remaining declared profiles
will be enabled as their event runtime is connected; the default build contains
only the completed climate environment.

`climate_tmp112` is the solar/supercapacitor build. It reports every 60 seconds
above 2500 mV and every 300 seconds at or below 2500 mV. Battery-powered climate
builds use one interval selected in their PlatformIO build environment; this is
not persisted or remotely configurable.

`climate_tmp112_debug` enables 9600-baud UART logging on PB2. It keeps the RTC
timebase active but never calls `sleep_cpu()`; idle iterations use a short
delay. This environment is for functional bring-up and commissioning
diagnostics, not sleep-current measurement.

Production and debug environments use the same serial UPDI upload settings as
`v2/node_test`: `serialupdi` on COM6 at 115200 baud. Upload a selected image
with `platformio run -e <environment> -t upload`.

PA6 is configured as an interrupt-driven active-low provisioning button. On an
unconfigured node, a debounced press wakes the MCU and requests commissioning
immediately instead of waiting for the periodic retry. Configured-node command
sessions and long-press factory reset remain future work.

Profile IDs describe only the byte-level telemetry and command contract. Gas,
water, door, and window presentation belongs to installation configuration and
Home Assistant.

The project structure and implementation constraints are defined in
`ARCHITECTURE.md`. Current implementation status and the exact continuation
point are recorded in `HANDOFF.md`. The production mapping assigns the reed/counter
input to PA5 and the provisioning/factory-reset button to PA6.

Run the host-side storage tests with:

```sh
wsl bash v2/node/scripts/run_native_tests_wsl.sh
```
