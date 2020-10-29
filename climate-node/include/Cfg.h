#ifndef CFG_h
#define CFG_h
#include <stdint.h>

namespace Cfg
{
    const uint16_t sendIntervalHigh = 60;
    const uint16_t sendIntervalLow = 300;

    const float lowVoltageThreshold = 2.5;

    const uint8_t maxSleepTime = 8;

    const int16_t addrVccCalibration = 0x3E8;

    const uint8_t ncPins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 14, 15, 16, 17};
}
#endif
