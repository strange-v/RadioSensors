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

void heartbeatTimerHandler()
{
    String id = ETH.macAddress();
    id.replace(':', '_');

    MqttMessage msg;
    sprintf(msg.topic, Cfg::gatewayTopic, id.c_str());
    sprintf(msg.data, "{\"u\":%lu,\"h\":%lu}", millis()/1000, ESP.getFreeHeap());
    msg.len = strlen(msg.data);
    msg.retain = false;
    
    xQueueSendToBack(qMqtt, &msg, QUEUE_SEND_DELAY);
}

void taskSendMqttMessages(void *pvParameters)
{
    MqttMessage msg;
    for (;;)
    {
        if (xQueueReceive(qMqtt, &msg, QUEUE_RECEIVE_DELAY))
        {
            _mqttPublish(msg.topic, msg.data, msg.len, msg.retain);
        }
    }
}

void onMqttConnect(bool sessionPresent)
{
    xTimerStop(tConectMqtt, TICKS_TO_WAIT12);
    xTimerStart(tHeartbeat, TICKS_TO_WAIT12);
    debugPrint("Connected to MQTT");

    //queueMqttDiscoveryMessages();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    xTimerStart(tConectMqtt, TICKS_TO_WAIT12);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
}

void queueMqttDiscoveryMessages()
{
    {
        MqttMessage msg = _composeMqttDiscoveryMessage("Uptime", "s", "{{ value_json.u }}");
        xQueueSendToBack(qMqtt, &msg, QUEUE_SEND_DELAY);
    }
    {
        MqttMessage msg = _composeMqttDiscoveryMessage("Heep", "bytes", "{{ value_json.h }}");
        xQueueSendToBack(qMqtt, &msg, QUEUE_SEND_DELAY);
    }
}

MqttMessage _composeMqttDiscoveryMessage(const char *name, const char *unit, const char *tpl)
{
    MqttMessage msg;
    StaticJsonDocument<512> doc;

    String id = ETH.macAddress();
    id.replace(':', '_');

    JsonObject dev  = doc.createNestedObject("dev");
    dev["name"] = Cfg::name;
    dev["sw"] = Cfg::version;
    dev["mf"] = Cfg::manufacturer;
    dev["mdl"] = Cfg::model;
    dev["ids"] = id.c_str();

    char uniqId[64];
    sprintf(uniqId, "%s_%s", id.c_str(), name);
    char topic[64];
    sprintf(topic, Cfg::gatewayTopic, id.c_str());

    doc["name"] = name;
    doc["uniq_id"] = uniqId;
    doc["stat_t"] = topic;
    if (strcmp(name, "Uptime") == 0)
    {
        doc["stat_cla"] = "total_increasing";
        doc["dev_cla"] = "duration";
    }
    else
    {
        doc["stat_cla"] = "measurement";
    }
    doc["unit_of_meas"] = unit;
    doc["frc_upd"] = true;
    doc["val_tpl"] = tpl;

    sprintf(msg.topic, "%s/sensor/%s/%s/config", Cfg::haDiscoveryPrefix, id.c_str(), name);
    serializeJson(doc, msg.data, sizeof(msg.data));

    msg.len = strlen(msg.data);
    msg.retain = true;

    return msg;
}

uint16_t _mqttPublish(const char *topic, const char *data, size_t length, bool retain)
{
    return mqtt.publish(topic, 2, retain, data, length);
}