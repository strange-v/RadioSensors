extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <Arduino.h>
#include <ArduinoOTA.h>
#ifdef ETH_PHY_TYPE
#include <ETH.h>
#else
#include <WiFi.h>
#endif
#include <SPI.h>
#include <RFM69.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <Module.h>
#include <Settings.h>
#include <Network.h>
#include <Mqtt.h>
#include <Radio.h>
#include <NodeData.h>

#ifdef TELNET_DEBUG
#include <ESPTelnet.h>
ESPTelnet telnet;
#endif

SPIClass spi;
RFM69 radio(RFM_CS, RFM_INT, RFM_HW, &spi);
AsyncMqttClient mqtt;
Settings moduleSettings;
bool ethConnected = false;

EventGroupHandle_t eg;
QueueHandle_t qData;
QueueHandle_t qMqtt;
TimerHandle_t tConectNetwork;
TimerHandle_t tConectMqtt;
SemaphoreHandle_t semaRadio;

void setup()
{
#ifdef SERIAL_DEBUG
  Serial.begin(115200);
  Serial.println(F("Starting..."));
#endif

  pinMode(RFM_INT, INPUT);

  EEPROM.begin(sizeof(Settings));
  moduleSettings = getSettings();

  WiFi.onEvent(networkEvent);
  configureMqtt();
  connectToNetwork();

  eg = xEventGroupCreate();
  qData = xQueueCreate(10, sizeof(NodeData));
  qMqtt = xQueueCreate(5, sizeof(MqttMessage));
  semaRadio = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(taskReadRadioMessage, "rr", TaskStack10K, NULL, Priority1, NULL, Core1);
  xTaskCreatePinnedToCore(taskProcessRadioMessage, "pr", TaskStack10K, NULL, Priority4, NULL, Core1);
  xTaskCreatePinnedToCore(taskSendMqttMessages, "mqtt", TaskStack10K, NULL, Priority5, NULL, Core1);

  tConectNetwork = xTimerCreate("cn", pdMS_TO_TICKS(20000), pdTRUE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(conectNetworkTimerHandler));
  tConectMqtt = xTimerCreate("cm", pdMS_TO_TICKS(10000), pdTRUE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqttTimerHandler));
  xTimerStart(tConectNetwork, 0);
  xTimerStart(tConectMqtt, 0);

  spi.begin(M_SCK, M_MISO, M_MOSI, RFM_CS);
  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
  debugPrint(radioState ? "RF69: init ok" : "RF69: error during init");
  radio.encrypt(RADIO_ENCRYPTION_KEY);
  radio.setIsrCallback(isrRadioMessage);
  radio.receiveDone();
}

void loop()
{
  ArduinoOTA.handle();
#ifdef TELNET_DEBUG
  telnet.loop();
#endif
}
