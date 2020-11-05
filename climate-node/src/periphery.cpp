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
  power_timer0_disable();
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
  //power_timer1_enable();
  //power_timer2_enable();
  power_twi_enable();
#ifdef NODE_DEBUG
  power_usart0_enable();
#endif

  delayMicroseconds(500);
}

void sleepMcu()
{
#ifdef NODE_PIN_DEBUG
  digitalWrite(3, LOW);
#endif
  // disable interrupts
  cli();
  // clear various "reset" flags
  MCUSR = 0;
  // allow changes, disable reset
  WDTCSR = bit(WDCE) | bit(WDE);
  // set interrupt mode and an interval
  WDTCSR = bit(WDIE) | bit(WDP3) | bit(WDP0); // set WDIE, and 8 seconds delay
  wdt_reset();
  // enable interrupts
  sei();

  turnAdcOff();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();

  sleep_disable();
  //turnAdcOn(); // Will be turned on later
#ifdef NODE_PIN_DEBUG
  digitalWrite(3, HIGH);
#endif
}

void prepareUnusedPins(const uint8_t pins[], uint8_t count)
{
  for (uint8_t i = 0; i < count; i++)
  {
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], LOW);
  }
}
