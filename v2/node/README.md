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

`climate_tmp112` currently uses a provisional 15-minute compile-time reporting
interval. It is intentionally independent from the one-hour rolling keep-alive
and one-minute counter aggregation policy for plain event nodes.

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
