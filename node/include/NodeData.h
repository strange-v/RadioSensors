#ifndef NODE_DATA_h
#define NODE_DATA_h
#include <stdint.h>

struct NodeData
{
  uint8_t type;
  int16_t temperature;
  int16_t humidity;
  int16_t pressure;
  int16_t vcc;
};
#endif
