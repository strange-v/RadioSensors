#ifndef FUNCTIONS_h
#define FUNCTIONS_h
#include <Arduino.h>
#include <RFM69.h>
#include <NodeData.h>
#include <periphery.h>

extern RFM69 radio;

void transmitData(NodeData data);

#endif
