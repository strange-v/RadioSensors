#ifndef NODE_DATA_h
#define NODE_DATA_h
#include <stdint.h>

struct __attribute__((__packed__)) NodeAData
{
	float temperature;
	float humidity;
	float vcc;
	uint8_t errors;
	uint32_t uptime;
};
#endif
