#ifndef MQTT_h
#define MQTT_h

#include <Arduino.h>
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <WiFi.h>
#include <AsyncMqttClient.h>
#include <Module.h>
#include <Settings.h>

extern EventGroupHandle_t eg;
extern QueueHandle_t qMqtt;
extern AsyncMqttClient mqtt;
extern TimerHandle_t tConectMqtt;
extern Settings moduleSettings;
extern bool ethConnected;

struct MqttMessage
{
    char topic[16];
    uint8_t len;
    char data[128];
};

void configureMqtt();
void connectToMqtt();
void connectToMqttTimerHandler();
void taskSendMqttMessages(void *pvParameters);
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
uint16_t _mqttPublish(const char *topic, const char *data, size_t length = 0U);

#endif