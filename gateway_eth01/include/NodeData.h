#ifndef NODE_DATA_h
#define NODE_DATA_h
#include <stdint.h>

struct __attribute__((__packed__)) NodeData
{
	uint16_t id;
	int16_t RSSI;
    uint8_t data[62];
    uint8_t length;
};
#endif