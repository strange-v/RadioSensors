#include <Mqtt.h>

void configureMqtt()
{
    mqtt.onConnect(onMqttConnect);
    mqtt.onDisconnect(onMqttDisconnect);
    mqtt.onMessage(onMqttMessage);
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCredentials(MQTT_USER, MQTT_PWD);
}

void connectToMqtt()
{
    if (ethConnected)
    {
        debugPrint("Connecting to MQTT...");
        mqtt.connect();
    }
    else
    {
        debugPrint("WiFi is off, won't reconnect MQTT");
    }
}

void connectToMqttTimerHandler()
{
    connectToMqtt();
}

void taskSendMqttMessages(void *pvParameters)
{
    MqttMessage msg;
    for (;;)
    {
        if (xQueueReceive(qMqtt, &msg, QUEUE_RECEIVE_DELAY))
        {
            _mqttPublish(msg.topic, msg.data, msg.len);
        }
    }
}

void onMqttConnect(bool sessionPresent)
{
    xTimerStop(tConectMqtt, TICKS_TO_WAIT12);
    debugPrint("Connected to MQTT");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    xTimerStart(tConectMqtt, TICKS_TO_WAIT12);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
}

uint16_t _mqttPublish(const char *topic, const char *data, size_t length)
{
    return mqtt.publish(topic, 2, false, data, length);
}