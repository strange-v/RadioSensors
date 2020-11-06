#ifndef FUNCTIONS_h
#define FUNCTIONS_h

#include <Arduino.h>
#include <RFM69.h>
#include <SparkFunHTU21D.h>
#include <Vcc.h>
#include <Cfg.h>
#include <NodeState.h>
#include <NodeData.h>
#include <periphery.h>

extern RFM69 radio;
extern HTU21D htu;
extern Vcc vcc;

extern NodeState nodeState;
extern uint8_t sleepCounter;
extern uint16_t currentSendInterval;
extern uint32_t time;
extern NodeData nodeData;
extern uint32_t nodeUptime;
extern uint8_t nodeSendErrors;

uint16_t getSendInterval(int16_t vcc);
void doSleep();
void nodeSleep(int16_t vcc);
void transmitData(NodeData data);
void handleSleepState();
void handleReadyState();

#endif