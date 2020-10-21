#include "Vcc.h"

Vcc::Vcc()
{
    _refCalibration = DEFAULT_REFERENCE_CALIBRATION;
}

float Vcc::getValue()
{
    return getValue_mV() / 1000.0;
}

uint16_t Vcc::getValue_mV()
{
    return _refCalibration / getInternal();
}

float Vcc::getPinVoltage(uint8_t pin)
{
    return analogRead(pin) / 1024 * getValue_mV() / 1000.0;
}

uint16_t Vcc::getPinVoltage_mV(uint8_t pin)
{
    return analogRead(pin) / 1024 * getValue_mV() / 1000.0;
}

float Vcc::getPinVoltage(uint8_t pin, float r1, float r2)
{
    return getPinVoltage(pin) / (r2 / (r1 + r2));
}

uint16_t Vcc::getPinVoltage_mV(uint8_t pin, float r1, float r2)
{
    return getPinVoltage_mV(pin) / (r2 / (r1 + r2));
}

void Vcc::applyCalibration(int32_t value)
{
    _refCalibration = value;
}

uint32_t Vcc::calculateCalibrationConstant(uint16_t milliVolts)
{
    return (uint32_t)milliVolts * getInternal();
}

#ifdef VCC_EEPROM
void Vcc::loadCalibrationFromEEPROM(int16_t address)
{
    EEPROM.get(address, _refCalibration);
}
void Vcc::storeCalibrationInEEPROM(int16_t address, int32_t value)
{
    EEPROM.put(address, value);
}
#endif

uint16_t Vcc::getInternal()
{
    if (ADMUX != ADMUX_VCCWRT1V1)
    {
        ADMUX = ADMUX_VCCWRT1V1;
        delayMicroseconds(500);
    }

    ADCSRA |= _BV(ADSC);
    while (bit_is_set(ADCSRA, ADSC))
    {
    };

    uint8_t low = ADCL;
    uint8_t high = ADCH;

    return (high << 8) | low;
}