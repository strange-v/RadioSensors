#include "Vcc.h"

float Vcc::getValue()
{
	const float internalVcc = 1.1;

	if (ADMUX != ADMUX_VCCWRT1V1)
	{
		ADMUX = ADMUX_VCCWRT1V1;
		delayMicroseconds(500);
	}

	ADCSRA |= _BV(ADSC);
	while (bit_is_set(ADCSRA, ADSC)) {};

	return internalVcc * 1023.0 / ADC;
}

float Vcc::getPercent(float min, float max)
{
	return 100.0 * (getValue() - min) / (max - min);
}
