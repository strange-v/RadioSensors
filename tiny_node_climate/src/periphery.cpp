#include <periphery.h>

void initRTC()
{
  while (RTC.STATUS > 0)
  {
    ; // wait for all registers to be synchronised
  }
  RTC.CLKSEL = RTC_CLKSEL_INT1K_gc; // 1.024kHz Internal Ultra-Low-Power Oscillator (from OSCULP32K)
  RTC.PITINTCTRL = RTC_PI_bm; // PIT Interrupt: enabled
  // RTC Clock Cycles 32768, resulting in 32768/1024Hz = every 32 seconds
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;        // enable PIT counter
}

uint16_t readVcc()
{
  // adc0 VREF = 1v1
  VREF.CTRLA = (VREF.CTRLA & ~VREF_ADC0REFSEL_gm) | VREF_ADC0REFSEL_1V1_gc;
  ADC0.CTRLB = ADC_SAMPNUM_ACC64_gc; // 64 samples
  ADC0.CTRLC = ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV16_gc | ADC_SAMPCAP_bm;
  ADC0.CTRLD = ADC_INITDLY_DLY64_gc;
  ADC0.MUXPOS = ADC_MUXPOS_INTREF_gc;
  ADC0.CTRLA = 1;   // enable adc
  ADC0.COMMAND = 1; // start
  while (ADC0.COMMAND);

  return (1024 * 11 * 100ul * 64) / ADC0.RES;
}

int8_t stablePinRead(pin_size_t pin, uint8_t numOfReadings)
{
  int8_t history[numOfReadings];
  int8_t head = 0;
  uint8_t i = 0;

  while (i < numOfReadings || !allEqual(history, numOfReadings))
  {
    history[head] = digitalRead(pin);
    head = (head + 1) % numOfReadings;
    i++;
    delay(1);
  }
  
  return history[0];
}

bool allEqual(int8_t arr[], uint8_t size) {
  int first = arr[0];
  for (int i = 1; i < size; i++) {
    if (arr[i] != first) {
      return false;
    }
  }
  return true;
}

void blink(int times, int interval)
{
#ifdef RADIO_NODE_DEBUG_LED    
  for (int n = 0; n < times; n++)
  {
    digitalWrite(RADIO_NODE_DEBUG_LED, HIGH);
    delay(interval);
    digitalWrite(RADIO_NODE_DEBUG_LED, LOW);
    delay(interval);
  }
#endif
}