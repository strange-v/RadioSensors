#ifndef PERIPHERY_h
#define PERIPHERY_h
#include <avr/sleep.h>
#include <Arduino.h>

#define PIN_REED_SWITCH PIN_PA6

void initRTC();
uint16_t readVcc();
int8_t stablePinRead(pin_size_t pin, uint8_t numOfReadings);
bool allEqual(int8_t arr[], uint8_t size);
void blink(int times = 1, int interval = 100);

#endif
