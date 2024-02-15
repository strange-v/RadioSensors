#include <periphery.h>

void turnAdcOff()
{
  ADCSRA = 0;
  power_adc_disable();
}

void turnAdcOn()
{
  power_adc_enable();
  ADCSRA = bit(ADEN);
}

void turnModulesOff()
{
  ADCSRA = 0;
  power_adc_disable();
  power_spi_disable();
// #ifndef NODE_DEBUG
//   power_timer0_disable();
// #endif
  power_timer1_disable();
  power_timer2_disable();
  power_twi_disable();
#ifndef NODE_DEBUG
  power_usart0_disable();
#endif
}

void turnModulesOn()
{
  power_adc_enable();
  ADCSRA = bit(ADEN);

  power_spi_enable();
  power_timer0_enable();
  //NOTE: Timers 1, 2 and twi interface are not used
  // power_timer1_enable();
  // power_timer2_enable();
  // power_twi_enable();
#ifdef NODE_DEBUG
  power_usart0_enable();
#endif

  delayMicroseconds(500);
}

void sleepMcu(bool shortPeriod)
{
  //NOTE: Disable interrupts
  cli();
  //NOTE: Clear various "reset" flags
  MCUSR = 0;
  //NOTE: Allow changes, disable reset
  WDTCSR = bit(WDCE) | bit(WDE);
  //NOTE: Set interrupt mode and an interval
  if (shortPeriod)
    WDTCSR = bit (WDIE) | bit (WDP2) | bit (WDP1); // set WDIE, and 1024 ms delay
  else
    WDTCSR = bit(WDIE) | bit(WDP3) | bit(WDP0); // set WDIE, and 8 seconds delay
  wdt_reset();
  //NOTE: Enable interrupts
  sei();

  turnAdcOff();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();

  sleep_disable();
  //NOTE: No need to turn on ADC, as it'll be done later
  // turnAdcOn();
}

void prepareUnusedPins(const uint8_t pins[], uint8_t count)
{
  for (uint8_t i = 0; i < count; i++)
  {
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], LOW);
  }
}

void clearInterrupt1Flag()
{
  EIFR |= bit(INTF1);
}
