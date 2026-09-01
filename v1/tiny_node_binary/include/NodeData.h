#ifndef NODE_DATA_h
#define NODE_DATA_h
#include <stdint.h>

struct NodeData
{
  uint8_t type;
  uint8_t state;
  int16_t vcc;
};
#endif
