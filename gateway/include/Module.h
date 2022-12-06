#ifndef MODULE_h
#define MODULE_h

#include <Arduino.h>
#include <time.h>

#define TaskStack10K 10000
#define TaskStack15K 15000
#define Priority1 5
#define Priority2 4
#define Priority3 3
#define Priority4 2
#define Priority5 1
#define Core0 0
#define Core1 1
#define QUEUE_RECEIVE_DELAY 10
#define QUEUE_SEND_DELAY 10
#define TICKS_TO_WAIT0 0
#define TICKS_TO_WAIT12 12

#define LED 2

#ifdef TELNET_DEBUG
#include <ESPTelnet.h>
extern ESPTelnet telnet;
#endif

void debugPrint(const char* text);
void debugPrintf(const char *format, ...);
void debugPrint(const IPAddress ip);
double round2(double value);

#endif