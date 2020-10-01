#pragma once
#include <stdint.h>

struct NodeData
{
	float temperature;
	float humidity;
	float vcc;
	uint8_t errors;
	uint32_t uptime;
};