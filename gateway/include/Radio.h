#ifndef RADIO_h
#define RADIO_h

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <Arduino.h>
#include <Module.h>
#include <RFM69.h>
#include <Event.h>
#include <Settings.h>
#include <NodeData.h>
#include <Mqtt.h>

extern Settings moduleSettings;
extern RFM69 radio;
extern EventGroupHandle_t eg;
extern QueueHandle_t qData;
extern QueueHandle_t qMqtt;
extern SemaphoreHandle_t semaRadio;

void IRAM_ATTR isrRadioMessage();
void taskReadRadioMessage(void *pvParameters);
void taskProcessRadioMessage(void *pvParameters);

#endif