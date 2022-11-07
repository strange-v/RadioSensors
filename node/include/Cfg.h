#ifndef CFG_h
#define CFG_h
#include <stdint.h>

namespace Cfg
{
  const uint16_t sendIntervalHigh = 63;
  const uint16_t sendIntervalLow = 300;
  const uint16_t lowVoltageThreshold = 2500;

  const uint8_t maxSleepTime = 9;
  const uint8_t maxRetryErrors = 9;

  const int16_t addrVccCalibration = 0x3E8;

  const uint8_t ncPins[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 14, 15, 16, 17};
}
#endif
