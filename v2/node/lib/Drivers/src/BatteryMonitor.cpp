#include "BatteryMonitor.h"

#include <Arduino.h>

namespace radiosensors {
namespace node {

uint16_t BatteryMonitor::readMillivolts() const {
    VREF.CTRLA =
        (VREF.CTRLA & ~VREF_ADC0REFSEL_gm) | VREF_ADC0REFSEL_1V1_gc;
    ADC0.CTRLB = ADC_SAMPNUM_ACC64_gc;
    // 500 kHz ADC clock at the 4 MHz CPU clock.
    ADC0.CTRLC = ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV8_gc | ADC_SAMPCAP_bm;
    ADC0.CTRLD = ADC_INITDLY_DLY64_gc;
    ADC0.MUXPOS = ADC_MUXPOS_INTREF_gc;
    ADC0.CTRLA = ADC_ENABLE_bm;
    ADC0.COMMAND = ADC_STCONV_bm;
    while ((ADC0.COMMAND & ADC_STCONV_bm) != 0) {}
    const uint16_t result = ADC0.RES;
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    return result == 0
        ? 0
        : static_cast<uint16_t>((1024UL * 1100UL * 64UL) / result);
}

}  // namespace node
}  // namespace radiosensors
