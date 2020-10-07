#pragma once
#include <stdint.h>

namespace Cfg
{
    const uint16_t sendIntervalHigh = 60;
    const uint16_t sendIntervalLow = 300;

    const float lowVoltageThreshold = 2.5;

    const uint8_t maxSleepTime = 8;
    
    const uint8_t pinVcc = 14;
    const uint8_t pinDivider = 15;
    const float r1 = 10000;
    const float r2 = 10000;
}