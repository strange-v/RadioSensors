#ifndef CFG_h
#define CFG_h
#include <stdint.h>

namespace Cfg
{
  const uint8_t pinPulse = 3;
  const uint16_t sendIntervalSec = 300;
  const uint8_t measurementDelayMs = 1;
  const int8_t numOfReadings = 5;
  const uint8_t maxRetryErrors = 9;

  const int16_t addrVccCalibration = 0x3E8;

  const uint8_t ncPins[] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 14, 15, 16, 17, 18, 19};
}
#endif
