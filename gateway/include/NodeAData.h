#ifndef NODE_DATA_h
#define NODE_DATA_h
#include <stdint.h>

struct __attribute__((__packed__)) NodeAData
{
	int16_t temperature;
	int16_t humidity;
	int16_t vcc;
};
#endif
