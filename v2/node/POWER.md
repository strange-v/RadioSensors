# Node power

Measured reference values and the battery budget model for battery-powered node images. Battery nodes target at least three years, preferably five, on one cell.

## Conditions

ATtiny1614 at 4 MHz, RFM69 H module at power level 2 (about 0 dBm on PA_BOOST), non-debug builds with unused pins disabled, supplied at 3.0 V by a Nordic PPK2 source meter. `counter_reed` and `binary` run on the internal board. `climate_tmp112` runs on the outdoor board, supplied directly at the MCU rail. The debug and SerialUPDI adapters are disconnected during measurement.

## Idle

| Image | Between wake-ups | One wake-up | Average |
| --- | ---: | --- | ---: |
| `counter_reed` | 1.75 µA | 0.27 µC, about 200 µs, 3.3 mA peak | 2.92 µA |
| `binary` | 1.71 µA | 0.27 µC | 2.90 µA |
| `climate_tmp112` | 2.23 µA | — | 2.2 µA |

Polled inputs wake every 250 ms; the climate image wakes every 32 s. The contact state does not measurably change these values.

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

On the internal board an acknowledged report without input work costs about 215 µC. The climate report adds the TMP112 one-shot conversion, which keeps the CPU awake together with the Vcc measurement for 32 ms (61 µC), and the outdoor board's module draws 30.6 mA while transmitting instead of 26 mA. The power-up peak is inrush into the board capacitors. The `binary` event divides as follows; the `counter_reed` report shows the same radio phases. The radio accounts for about 90 % of a report, and its airtime is set by the bit rate: both the frame and the ACK are padded to one 16-byte AES block.

| Phase | Duration | Mean | Charge |
| --- | ---: | ---: | ---: |
| Debounce burst and Vcc measurement | 8.4 ms | 1.7 mA | 14 µC |
| Channel check before transmission | 0.5 ms | 16 mA | 8 µC |
| Transmission | 3.9 ms | 26 mA | 102 µC |
| Waiting for the ACK | 4.3 ms | 17.6 mA | 75 µC |
| Receiver on after the ACK | 0.4 ms | 17 mA | 7 µC |
| Vcc measurement after transmission | 3.5 ms | 1.8 mA | 6 µC |

Not yet measured: a transmission without ACK and commissioning receive windows.

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

The solar climate node needs about 6.5 µA while reporting every 64 s and about 3.1 µA at the 320-second low-charge interval.

The counter needs about 30 mAh per year and the binary input about 27 mAh: seven to eight years of a 220 mAh CR2032 before self-discharge and end-of-life voltage sag under the transmit peak. The idle current dominates; the hourly keep-alive costs about 2 % of the budget.

## Measuring

- Measure the charge of each event in the PPK2 selection, not the average of an arbitrary window, and combine the charges with the model above.
- Supply from the PPK2 source meter at the voltage of the intended cell.
- Charges from a hand-held magnet include extra debounce bursts caused by reed chatter; measure a counter on a running meter for realistic values.
