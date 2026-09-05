#include "LowPowerClock.h"

#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

namespace {
volatile uint32_t elapsedMs = 0;
}

ISR(RTC_PIT_vect) {
    RTC.PITINTFLAGS = RTC_PI_bm;
    elapsedMs += 32000UL;
}

namespace radiosensors {
namespace node {

void LowPowerClock::begin() {
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    TCA0.SPLIT.CTRLA &= ~TCA_SPLIT_ENABLE_bm;
    while (RTC.STATUS != 0) {}
    RTC.CLKSEL = RTC_CLKSEL_INT1K_gc;
    RTC.PITINTCTRL = RTC_PI_bm;
    RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();
}

uint32_t LowPowerClock::nowMs() const {
    const uint8_t status = SREG;
    cli();
    const uint32_t value = elapsedMs;
    SREG = status;
    return value;
}

void LowPowerClock::sleepUntilInterrupt() const {
    sleep_cpu();
}

}  // namespace node
}  // namespace radiosensors
