#ifndef FUNCTIONS_h
#define FUNCTIONS_h

#include <Arduino.h>
#include <RFM69.h>
#include <eewl.h>
#include <Vcc.h>
#include <Cfg.h>
#include <NodeState.h>
#include <NodeData.h>
#include <StoragePackage.h>
#include <periphery.h>

extern StoragePackage package;

extern RFM69 radio;
extern Vcc vcc;
extern EEWL storage;

extern NodeData nodeData;
extern uint8_t nodeSendErrors;


void handlePulse();
bool transmitData(NodeData data);
void doSleep(bool shortPeriod);

#endif