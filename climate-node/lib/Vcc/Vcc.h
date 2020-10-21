#pragma once
#include <Arduino.h>
#ifdef VCC_EEPROM
#include <EEPROM.h>
#endif

#ifndef DEFAULT_REFERENCE_CALIBRATION
#define DEFAULT_REFERENCE_CALIBRATION 1125300L
#endif

#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
#define ADMUX_VCCWRT1V1 (_BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1))
#elif defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
#define ADMUX_VCCWRT1V1 (_BV(MUX5) | _BV(MUX0))
#elif defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
#define ADMUX_VCCWRT1V1 (_BV(MUX3) | _BV(MUX2))
#else
#define ADMUX_VCCWRT1V1 (_BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1))
#endif

class Vcc
{
public:
    Vcc();
	float getValue();
	uint16_t getValue_mV();
    float getPinVoltage(uint8_t pin);
    uint16_t getPinVoltage_mV(uint8_t pin);
    float getPinVoltage(uint8_t pin, float r1, float r2);
    uint16_t getPinVoltage_mV(uint8_t pin, float r1, float r2);
    void applyCalibration(int32_t value);
    uint32_t calculateCalibrationConstant(uint16_t milliVolts);
#ifdef VCC_EEPROM
    void loadCalibrationFromEEPROM(int16_t address);
    void storeCalibrationInEEPROM(int16_t address, int32_t value);
#endif
private:
    uint16_t getInternal();
    uint32_t _refCalibration;
};

