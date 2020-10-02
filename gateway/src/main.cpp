extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <RFM69.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <Cfg.h>
#include <Cfg.h>
#include <NodeAData.h>
#include <JsonData.h>

#define TaskStack10K 10000
#define Priority1 1
#define Priority2 2
#define Priority3 3
#define Priority4 4
#define Priority5 5
#define Core0 0
#define Core1 1
#define TICKS_TO_WAIT12 12

#define EVENT_WIFI_CONNECT (1 << 0)             //1
#define EVENT_MQTT_CONNECT (1 << 1)             //10
#define EVENT_SEND_RECONNECT_TELEMETRY (1 << 2) //100

RFM69 radio;
AsyncMqttClient mqttClient;

EventGroupHandle_t eg;
SemaphoreHandle_t sema_MQTT;
QueueHandle_t xQ_Data;

#pragma region Helper Functions
double round2(double value)
{
  return (int)(value * 100 + 0.5) / 100.0;
}
#pragma endregion

#pragma region Radio
void proccessNodeA()
{
  NodeAData data;
  memcpy(&data, &radio.DATA, radio.DATALEN);
#ifdef GW_DEBUG
  Serial.print(F("RSSI:"));
  Serial.println(radio.RSSI);
  Serial.print(F("T:"));
  Serial.println(data.temperature);
  Serial.print(F("H:"));
  Serial.println(data.humidity);
  Serial.print(F("V:"));
  Serial.println(data.vcc);
  Serial.print(F("U:"));
  Serial.println(data.uptime);
#endif

//   StaticJsonDocument<256> doc;
//   JsonObject object = doc.to<JsonObject>();
//   object["id"] = radio.SENDERID;
//   object["t"] = round2(data.temperature);
//   object["h"] = round2(data.humidity);
//   object["v"] = round2(data.vcc);
//   object["e"] = data.errors;
//   object["u"] = data.uptime;

//   JsonData jsonData{radio.SENDERID};
// #ifdef GW_DEBUG
//   serializeJsonPretty(doc, Serial);
// #endif
//   serializeJson(doc, jsonData.msg, sizeof(jsonData.msg));
  //xQueueSendToBack(xQ_Data, &jsonData, TICKS_TO_WAIT12);
}

void radioLoop()
{
  if (radio.receiveDone())
  {
#ifdef GW_DEBUG
    Serial.printf("Received from node %d\n", radio.SENDERID);
    Serial.printf("Length: %d\n", radio.DATALEN);
    Serial.printf("RSSI: %d\n", radio.RSSI);
#endif
    switch (radio.SENDERID)
    {
    case Cfg::idNodeA:
      proccessNodeA();
      break;
    default:
      break;
    }

    if (radio.ACKRequested())
    {
      radio.sendACK();
#ifdef GW_DEBUG
      Serial.printf("ACK sent to node %d\n", radio.SENDERID);
#endif
    }
  }
}
#pragma endregion

#pragma region WiFi
void connectToWifi()
{
  if (!WiFi.isConnected())
  {
    Serial.println("Connecting to Wi-Fi...");
    WiFi.begin(Cfg::wifiSsid, Cfg::wifiPassword);
  }
}

void WiFiEvent(WiFiEvent_t event)
{
#ifdef GW_DEBUG
  Serial.printf("[WiFi-event] event: %d\n", event);
#endif
  switch (event)
  {
  case SYSTEM_EVENT_STA_GOT_IP:
#ifdef GW_DEBUG
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
#endif
    xEventGroupClearBits(eg, EVENT_WIFI_CONNECT);
    xEventGroupSetBits(eg, EVENT_MQTT_CONNECT);
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
#ifdef GW_DEBUG
    Serial.println("WiFi lost connection");
#endif
    xEventGroupSetBits(eg, EVENT_WIFI_CONNECT);
    break;
  }
}

void TaskConnectToWifi(void *pvParameters)
{
  for (;;)
  {
    xEventGroupWaitBits(eg, EVENT_WIFI_CONNECT, pdFALSE, pdTRUE, portMAX_DELAY);
    connectToWifi();

    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
#pragma endregion

#pragma region MQTT
void connectToMqtt()
{
#ifdef GW_DEBUG
  Serial.println(F("Connecting to MQTT..."));
#endif
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent)
{
#ifdef GW_DEBUG
  Serial.println(F("Connected to MQTT."));
#endif

  xEventGroupClearBits(eg, EVENT_MQTT_CONNECT);
  //mqttClient.subscribe("/wc/light", 2);

  xEventGroupSetBits(eg, EVENT_SEND_RECONNECT_TELEMETRY);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
#ifdef GW_DEBUG
  Serial.println(F("Disconnected from MQTT."));
#endif
  if (WiFi.isConnected())
  {
    xEventGroupSetBits(eg, EVENT_MQTT_CONNECT);
  }
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  // if (strcmp(topic, mqttLightTopic) == 0)
  // {
  // }
}

void sendReconnectTelemetry()
{
  char dataBuffer[64];
  char lineBuffer[24];
  dataBuffer[0] = '\0';
  lineBuffer[0] = '\0';

  WiFi.localIP().toString().toCharArray(lineBuffer, 24);
  sprintf(dataBuffer, "%s;%s;%lu\n", Cfg::mqttNodeId, lineBuffer, millis());
  mqttClient.publish("/reboot", 2, false, dataBuffer);
}

void TaskConnectToMqtt(void *pvParameters)
{
  for (;;)
  {
    xEventGroupWaitBits(eg, EVENT_MQTT_CONNECT, pdFALSE, pdTRUE, portMAX_DELAY);
    connectToMqtt();

    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void TaskSendTelemetry(void *pvParameters)
{
  for (;;)
  {
    if (xSemaphoreTake(sema_MQTT, TICKS_TO_WAIT12) == pdTRUE)
    {
      xEventGroupWaitBits(eg, EVENT_SEND_RECONNECT_TELEMETRY, pdTRUE, pdTRUE, portMAX_DELAY);
      sendReconnectTelemetry();
      xSemaphoreGive(sema_MQTT);
    }
  }
}

void TaskSendData(void *pvParameters)
{
  for (;;)
  {
  }
}
#pragma endregion

void setup()
{
#ifdef GW_DEBUG
  Serial.begin(115200);
  Serial.println(F("Starting..."));
#endif

  WiFi.persistent(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.onEvent(WiFiEvent);

  ArduinoOTA.setPassword(Cfg::otaPassword);
  ArduinoOTA.begin();

  eg = xEventGroupCreate();

  sema_MQTT = xSemaphoreCreateMutex();
  xSemaphoreGive(sema_MQTT);

  //xQ_Data = xQueueCreate(10, sizeof(JsonData));

  xTaskCreatePinnedToCore(TaskConnectToWifi, "tConnectToWiFi", TaskStack10K, NULL, Priority1, NULL, Core1);
  xTaskCreatePinnedToCore(TaskConnectToMqtt, "tConnectToMqtt", TaskStack10K, NULL, Priority2, NULL, Core1);
  xTaskCreatePinnedToCore(TaskSendTelemetry, "tSendTelemetry", TaskStack10K, NULL, Priority5, NULL, Core1);
  //xTaskCreatePinnedToCore(TaskSendData, "tSendData", TaskStack10K, NULL, Priority2, NULL, Core1);

  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef GW_DEBUG
  Serial.println(radioState ? F("RF69: init ok") : F("RF69: error during init"));
#endif
  radio.encrypt(Cfg::radioEncriptionKey);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setServer(Cfg::mqttHost, Cfg::mqttPort);
  mqttClient.setCredentials(Cfg::mqttUser, Cfg::mqttPassword);

  xEventGroupSetBits(eg, EVENT_WIFI_CONNECT);
}

void loop()
{
  ArduinoOTA.handle();
  radioLoop();
}