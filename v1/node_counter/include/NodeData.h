#ifndef NODE_DATA_h
#define NODE_DATA_h
#include <stdint.h>

struct NodeData
{
  uint8_t type;
  int32_t counter;
  int16_t vcc;
};
#endif
