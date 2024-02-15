#ifndef PERIPHERY_h
#define PERIPHERY_h

#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>

void turnAdcOff();
void turnAdcOn();
void turnModulesOff();
void turnModulesOn();
void sleepMcu(bool shortPeriod = false);
void prepareUnusedPins(const uint8_t pins[], uint8_t count);
void clearInterrupt1Flag();

#endif
