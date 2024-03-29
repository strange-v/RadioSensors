#ifndef NETWORK_h
#define NETWORK_h

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#ifdef ETH_PHY_TYPE
#include <ETH.h>
#else
#include <WiFi.h>
#endif
#include <ArduinoOTA.h>
#include <Mqtt.h>
#include <Module.h>
#include <Settings.h>

#ifdef TELNET_DEBUG
#include <ESPTelnet.h>
extern ESPTelnet telnet;
#endif

extern bool ethConnected;
extern Settings moduleSettings;

void networkEvent(WiFiEvent_t event);
void connectToNetwork();
void conectNetworkTimerHandler();
void initOta();

#endif