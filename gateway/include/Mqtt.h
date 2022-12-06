#ifndef MQTT_h
#define MQTT_h

#include <Arduino.h>
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <ETH.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <Module.h>
#include <Cfg.h>
#include <Settings.h>

extern EventGroupHandle_t eg;
extern QueueHandle_t qMqtt;
extern AsyncMqttClient mqtt;
extern TimerHandle_t tConectMqtt;
extern TimerHandle_t tHeartbeat;
extern Settings moduleSettings;
extern bool ethConnected;

struct MqttMessage
{
    char topic[64];
    char data[512];
    uint16_t len;
    bool retain;
};

void configureMqtt();
void connectToMqtt();
void connectToMqttTimerHandler();
void heartbeatTimerHandler();
void taskSendMqttMessages(void *pvParameters);
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void queueMqttDiscoveryMessages();
MqttMessage _composeMqttDiscoveryMessage(const char *name, const char *unit, const char *tpl);
uint16_t _mqttPublish(const char *topic, const char *data, size_t length = 0U, bool retain = false);

#endif