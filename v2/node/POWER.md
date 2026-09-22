# Node power

Measured reference values and the battery budget model for battery-powered node images. Battery nodes target at least three years, preferably five, on one cell.

## Conditions

ATtiny1614 at 4 MHz, RFM69 H module at power level 2 (about 0 dBm on PA_BOOST) unless a row names a lowered level, non-debug builds with unused pins disabled, supplied at 3.0 V by a Nordic PPK2 source meter. `counter_reed` and `binary` run on the internal board. `climate_tmp112` runs on the outdoor board, supplied at the MCU rail unless a row names the supercap connector, which adds the supervisor, load switch, and charging diode. The debug and SerialUPDI adapters are disconnected during measurement.

## Idle

| Image | Between wake-ups | One wake-up | Average |
| --- | ---: | --- | ---: |
| `counter_reed` | 1.75 µA | 0.27 µC, about 200 µs, 3.3 mA peak | 2.92 µA |
| `binary` | 1.71 µA | 0.27 µC | 2.90 µA |
| `climate_tmp112` | 2.23 µA | — | 2.2 µA |
| `climate_tmp112`, supercap connector | 2.99 µA | — | 3.0 µA |

Polled inputs wake every 250 ms; the climate image wakes every 32 s. The contact state does not measurably change these values. The supercap supply path adds about 0.76 µA.

## Input events

| Event | Duration | Mean | Charge |
| --- | ---: | ---: | ---: |
| Debounce burst on a contact change | 4.7 ms | 1.56 mA | 7.3 µC |
| Accepted counter pulse, EEPROM write | 20.9 ms | 1.68 mA | 35 µC |

A counter pulse costs two debounce bursts, one when the contact closes and one when it opens, plus the EEPROM write when the minimum-phase filter accepts the rise: about 50 µC. The write keeps the CPU waiting on the NVM controller for about 16 ms at 1.7 mA; a debounce burst runs at about 1.55 mA. Reed chatter adds bursts and can stretch one to its 20-sample limit, about 20 ms.

## Reports

| Event | Duration | Mean | Peak | Charge |
| --- | ---: | ---: | ---: | ---: |
| `binary` state change and acknowledged report | 22.3 ms | 9.8 mA | 29.8 mA | 218 µC |
| `counter_reed` accepted pulse and acknowledged report in one wake-up | 34.7 ms | 7.2 mA | 30.4 mA | 251 µC |
| `climate_tmp112` acknowledged report | 46.5 ms | 5.9 mA | 31.6 mA | 274 µC |
| `climate_tmp112` power-up to first acknowledged report | 88.4 ms | 4.7 mA | 84.6 mA | 414 µC |
| `climate_tmp112` power-up to first acknowledged report at a lowered level, supercap connector | 88.6 ms | 4.2 mA | 289 mA | 370 µC |

On the internal board an acknowledged report without input work costs about 215 µC. The climate report adds the TMP112 one-shot conversion, which keeps the CPU awake together with the Vcc measurement for 32 ms (61 µC), and the outdoor board's module draws 30.6 mA while transmitting at level 2 and about 22 mA at the lowered level. The power-up peak is inrush into the node's capacitors, at most 0.1 ms. The `binary` event divides as follows; the `counter_reed` report shows the same radio phases. The radio accounts for about 90 % of a report, and its airtime is set by the bit rate: both the frame and the ACK are padded to one 16-byte AES block.

| Phase | Duration | Mean | Charge |
| --- | ---: | ---: | ---: |
| Debounce burst and Vcc measurement | 8.4 ms | 1.7 mA | 14 µC |
| Channel check before transmission | 0.5 ms | 16 mA | 8 µC |
| Transmission | 3.9 ms | 26 mA | 102 µC |
| Waiting for the ACK | 4.3 ms | 17.6 mA | 75 µC |
| Receiver on after the ACK | 0.4 ms | 17 mA | 7 µC |
| Vcc measurement after transmission | 3.5 ms | 1.8 mA | 6 µC |

Not yet measured: a transmission without ACK, commissioning receive windows, and command sessions.

## Radio power levels

`radio_power_sweep` at 3.0 V: one voltage-only report per level, each acknowledged on the first attempt and taking 17.7–18.0 ms whatever the level. Nominal output follows the RFM69 library 1.6.0 mapping for H modules. Columns name the board and antenna. Each cell gives the transmission current, the highest 2 ms average inside the transmission and therefore above the phase means in [Reports](#reports), and the charge of the whole report.

| Level | Nominal output | Amplifier | Internal, sticker | Outdoor, sticker | Outdoor, 5 cm | Outdoor, 20 cm |
| ---: | ---: | --- | ---: | ---: | ---: | ---: |
| 0 | −2 dBm | PA1 | 26.6 mA, 199 µC | 28.8 mA, 211 µC | 23.7 mA, 192 µC | 27.5 mA, 204 µC |
| 1 | −1 dBm | PA1 | 28.3 mA, 204 µC | 30.6 mA, 218 µC | 24.7 mA, 196 µC | 28.9 mA, 210 µC |
| 2 | 0 dBm | PA1 | 30.4 mA, 214 µC | 32.8 mA, 225 µC | 25.9 mA, 198 µC | 30.7 mA, 217 µC |
| 3 | 1 dBm | PA1 | 32.7 mA, 222 µC | 35.2 mA, 235 µC | 26.8 mA, 202 µC | 32.3 mA, 223 µC |
| 4 | 2 dBm | PA1 | 35.4 mA, 232 µC | 38.1 mA, 245 µC | 27.9 mA, 208 µC | 34.3 mA, 231 µC |
| 5 | 3 dBm | PA1 | 38.6 mA, 244 µC | 41.3 mA, 257 µC | 29.1 mA, 211 µC | 36.4 mA, 238 µC |
| 6 | 4 dBm | PA1 | 41.7 mA, 256 µC | 44.5 mA, 269 µC | 30.3 mA, 224 µC | 38.3 mA, 246 µC |
| 7 | 5 dBm | PA1 | 45.2 mA, 269 µC | 47.9 mA, 283 µC | 31.8 mA, 221 µC | 40.7 mA, 263 µC |
| 8 | 6 dBm | PA1 | 49.2 mA, 284 µC | 51.8 mA, 296 µC | 33.6 mA, 228 µC | 43.4 mA, 267 µC |
| 9 | 7 dBm | PA1 | 52.6 mA, 298 µC | 55.1 mA, 311 µC | 35.3 mA, 243 µC | 46.2 mA, 277 µC |
| 10 | 8 dBm | PA1 | 56.0 mA, 310 µC | 58.2 mA, 322 µC | 37.4 mA, 243 µC | 49.1 mA, 285 µC |
| 11 | 9 dBm | PA1 | 58.7 mA, 321 µC | 60.7 mA, 330 µC | 39.7 mA, 253 µC | 52.0 mA, 299 µC |
| 12 | 10 dBm | PA1 | 60.7 mA, 328 µC | 62.6 mA, 339 µC | 42.2 mA, 260 µC | 54.9 mA, 312 µC |
| 13 | 11 dBm | PA1 | 62.2 mA, 334 µC | 62.2 mA, 338 µC | 44.9 mA, 272 µC | 57.8 mA, 330 µC |
| 14 | 12 dBm | PA1 | 62.0 mA, 335 µC | 62.7 mA, 339 µC | 48.0 mA, 284 µC | 60.4 mA, 331 µC |
| 15 | 13 dBm | PA1 | 62.8 mA, 335 µC | 63.3 mA, 342 µC | 51.8 mA, 296 µC | 62.4 mA, 347 µC |
| 16 | 12 dBm | PA1 + PA2 | 76.5 mA, 387 µC | 87.3 mA, 432 µC | 51.6 mA, 298 µC | 70.2 mA, 368 µC |
| 17 | 13 dBm | PA1 + PA2 | 85.1 mA, 421 µC | 95.1 mA, 462 µC | 54.3 mA, 306 µC | 74.7 mA, 383 µC |
| 18 | 14 dBm | PA1 + PA2 | 94.3 mA, 456 µC | 103.6 mA, 493 µC | 56.7 mA, 316 µC | 79.5 mA, 404 µC |
| 19 | 15 dBm | PA1 + PA2 | 104.1 mA, 492 µC | 111.9 mA, 528 µC | 60.1 mA, 329 µC | 84.6 mA, 424 µC |
| 20 | 17 dBm | PA1 + PA2, high power | 123.6 mA, 564 µC | 130.3 mA, 594 µC | 76.3 mA, 388 µC | 102.0 mA, 485 µC |
| 21 | 18 dBm | PA1 + PA2, high power | 130.4 mA, 591 µC | 135.3 mA, 610 µC | 84.3 mA, 420 µC | 111.0 mA, 524 µC |
| 22 | 19 dBm | PA1 + PA2, high power | 135.2 mA, 605 µC | 139.3 mA, 627 µC | 93.7 mA, 455 µC | 121.2 mA, 570 µC |
| 23..31 | 20 dBm | PA1 + PA2, high power | 139.6 mA, 625 µC | 142.6 mA, 640 µC | 104.0 mA, 494 µC | 130.6 mA, 596 µC |

- The library clamps every level above 23 to 23, so a ceiling above 23 changes nothing. The highest single sample is 147 mA.
- On every board and antenna levels 16 and 17 draw more than 14 and 15 for the same nominal output, and the step from 15 to 16 lowers it. The scale is one nominal dB per level only within 0–15 and within 16–19 or 20–23.
- The antenna changes the current far more than the board. With the sticker antenna the two boards stay within 14 % of each other and both draw the same current at levels 13–15; on the outdoor board the 20 cm antenna draws up to 20 % less than the sticker and the 5 cm one up to 46 % less. Amplifier current follows the antenna load, not the radiated power, so a ceiling needs each board and antenna's own sweep together with the gateway's RSSI.
- A report at level 23 costs 2.5–2.9 times one at level 2. On the outdoor board reporting every 64 s that adds 4.6–6.5 µA; for an internal-board node with about 64 reports a day, about 2.7 mAh per year.

## Battery budget

```text
I_avg = I_idle + f_event · Q_event + f_report · Q_report
```

One report costs about 0.06 µAh; 1 mAh equals 3.6 C.

| Image | Assumption | mAh per year |
| --- | --- | ---: |
| `counter_reed` | idle 2.92 µA | 25.6 |
| | 150,000 pulses at 50 µC | 2.1 |
| | about four pulses per minute, one report per active minute | 2.2 |
| | hourly keep-alive, at most | 0.5 |
| `binary` | idle 2.90 µA | 25.4 |
| | 40 state changes per day | 0.9 |
| | hourly keep-alive, at most | 0.5 |

Supplied through the supercap connector, the solar climate node needs about 7.3 µA while reporting every 64 s and about 3.9 µA at the 320-second low-charge interval.

The counter needs about 30 mAh per year and the binary input about 27 mAh: seven to eight years of a 220 mAh CR2032 before self-discharge and end-of-life voltage sag under the transmit peak. The idle current dominates; the hourly keep-alive costs about 2 % of the budget.

## Measuring

- Measure the charge of each event in the PPK2 selection, not the average of an arbitrary window, and combine the charges with the model above.
- Supply from the PPK2 source meter at the voltage of the intended cell.
- Charges from a hand-held magnet include extra debounce bursts caused by reed chatter; measure a counter on a running meter for realistic values.
