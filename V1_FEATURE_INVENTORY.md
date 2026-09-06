# V1 Firmware Feature Inventory

Temporary migration reference. Move this file to archive or delete it after v2 node feature-parity review.

Purpose: regression checklist for preserving functionality while replacing the legacy firmware. Scope: projects under `v1/`; `v1/gateway` is intentionally excluded.

## Shared radio behavior

- RFM69 communication at 868 MHz.
- Configurable node ID, gateway ID, network ID, transmit power, and encryption key supplied at build time.
- Binary, fixed-layout payloads sent to gateway node `100` with acknowledgements/retries.
- Radio enters sleep mode after initialization and after transmissions.
- Optional compile-time debug output or GPIO/LED signaling, depending on the project.
- Battery voltage is included in every payload, in millivolts.

## `node` — ATmega328P climate node

- Supports compile-time selection of HTU21D, BME280, or SHT31 temperature/humidity sensors.
- BME280 uses forced single-sample mode with filtering disabled.
- Sends message type `3`: temperature (°C × 100), humidity (% RH × 100), and VCC (mV).
- Validates temperature (`-80..125 °C`, finite) and humidity (`0..100%`, finite).
- Retries a sensor read once and independently substitutes valid temperature/humidity values from the retry.
- Encodes unavailable/invalid sensor values as `INT16_MIN`.
- Uses acknowledged radio delivery: two retries normally; disables retries after more than nine consecutive failed sends; resets the error count after success.
- Uses an adaptive reporting interval based on battery voltage:
  - Above 2500 mV: approximately 63 seconds.
  - At or below 2500 mV: approximately 297 seconds (configured target: 300 seconds).
- Measures VCC through the Vcc library and optionally loads its calibration from EEPROM address `0x3E8`.
- Tracks approximate node uptime internally for debug output.
- Uses watchdog-interrupt power-down sleep (approximately 8-second cycles); a delay-based fallback is available at compile time.
- Disables unused peripherals and drives unused pins low to reduce power consumption.
- Has build profiles for node IDs `2`, `3`, and `10`, including sensor and high-power-radio variants.

## `node_counter` — ATmega328P pulse counter

- Debounces a pull-up pulse input on pin `3` using five equal readings spaced 1 ms apart.
- Counts completed pulses on the rising/end edge (`LOW` to `HIGH`).
- Persists the cumulative signed 32-bit counter in EEPROM using wear leveling (100-entry buffer starting at address `0x1`).
- Restores the counter from EEPROM after startup.
- Writes EEPROM only when the persisted count differs from the current count.
- Sends message type `11`: cumulative counter and VCC (mV).
- Sends on every completed pulse; also contains a state-change-triggered status-send path tied to the 300-second sleep counter.
- Uses acknowledged radio delivery: two retries normally; disables retries after more than nine consecutive failed sends; resets the error count after success.
- Measures VCC for each sent update; optional VCC calibration support exists in code.
- Uses watchdog-interrupt power-down sleep at approximately 1.024-second polling intervals; an 8-second sleep mode and delay-based fallback also exist.
- Disables unused peripherals and drives unused pins low to reduce power consumption.
- Provides a build profile for node ID `15`.

## `tiny_node_binary` — ATtiny1614 binary/reed-switch node

- Polls a pull-up reed/binary input on `PA6` every 0.25 seconds using the RTC periodic interrupt timer.
- Detects both input-state transitions and confirms them with five stable readings at 1 ms spacing.
- Sends only confirmed state changes; no initial-state report is sent while the input remains at its default `HIGH` state.
- Sends message type `21`: binary state and VCC (mV).
- Measures VCC using the internal 1.1 V reference with 64-sample ADC accumulation.
- Uses acknowledged delivery with up to three retries.
- Uses power-down sleep between polls and disables ADC and TCA0 when idle.
- Supports optional debug LED status: one blink for success, three for initialization/transmission failure.
- Provides a build profile for node ID `5` with high-power RFM69 mode enabled and transmit power level `2`.

## `tiny_node_climate` — ATtiny1614 climate node

- Reads an SHT31 sensor at I2C address `0x44` every 32 seconds using the RTC periodic interrupt timer.
- Sends message type `3`: temperature (°C × 100), humidity (% RH × 100), and VCC (mV).
- Updates temperature and humidity only after a successful combined sensor read; otherwise retains the previous values (initially zero).
- Samples battery voltage both before and after transmission and reports the lower of the current pre-transmission value and the previous post-transmission value, capturing radio-load voltage sag.
- Measures VCC using the internal 1.1 V reference with 64-sample ADC accumulation.
- Uses acknowledged delivery with up to three retries.
- Uses power-down sleep between reports and disables ADC and TCA0 when idle.
- Supports optional debug LED status: one blink for success, three for initialization/transmission failure.
- Provides a build profile for node ID `7` with high-power RFM69 mode enabled and transmit power level `2`.

## Payload compatibility checklist

| Message type | Producer(s) | Payload fields and order |
|---|---|---|
| `3` | `node`, `tiny_node_climate` | `uint8 type`, `int16 temperature`, `int16 humidity`, `int16 vcc` |
| `11` | `node_counter` | `uint8 type`, `int32 counter`, `int16 vcc` |
| `21` | `tiny_node_binary` | `uint8 type`, `uint8 state`, `int16 vcc` |

All payloads are transmitted as raw in-memory structs; field order, integer width, signedness, and compiler padding are therefore compatibility-sensitive.
