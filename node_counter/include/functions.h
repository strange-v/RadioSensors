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

extern volatile NodeState nodeState;
extern uint8_t sleepCounter;
extern volatile bool pulsePinState;
extern volatile int32_t pulseCounter;
extern volatile uint8_t staleCounter;
extern uint32_t time;
extern NodeData nodeData;
extern uint8_t nodeSendErrors;
extern void onPulse();

void handleSleepState();
void handleStaleSleepState();
void handlePulseState(uint32_t lastPulseTime);
void handleReadyState(int32_t counter);
void doSleep(bool shortPeriod = false);
bool transmitData(NodeData data);

#endif