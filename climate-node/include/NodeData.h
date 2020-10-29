#ifndef NODE_DATA_h
#define NODE_DATA_h
#include <stdint.h>

struct NodeData
{
	float temperature;
	float humidity;
	float vcc;
	uint8_t errors;
	uint32_t uptime;
};
#endif
