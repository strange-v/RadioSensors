#include <Radio.h>

void isrRadioMessage()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(eg, EVENT_RADIO_MESSAGE, &xHigherPriorityTaskWoken);
}

void taskReadRadioMessage(void *pvParameters)
{
    for (;;)
    {
        xEventGroupWaitBits(eg, EVENT_RADIO_MESSAGE, pdTRUE, pdTRUE, portMAX_DELAY);

        if (xSemaphoreTake(semaRadio, portMAX_DELAY) == pdTRUE)
        {
            if (radio.receiveDone())
            {
                debugPrintf("Received from node %d\n", radio.SENDERID);
                debugPrintf("Length: %d\n", radio.DATALEN);
                debugPrintf("RSSI: %d\n", radio.RSSI);

                NodeData nodeData = {radio.SENDERID, radio.RSSI};
                memcpy(&nodeData.data, &radio.DATA, radio.DATALEN);
                nodeData.length = radio.DATALEN;

                if (radio.ACKRequested())
                {
                    radio.sendACK();
                    debugPrintf("ACK sent to node %d\n", radio.SENDERID);
                }

                if (xQueueSendToBack(qData, &nodeData, QUEUE_RECEIVE_DELAY) != pdPASS)
                    debugPrint("Failed add to data queue");
            }
            xSemaphoreGive(semaRadio);
        }
    }
}

void taskProcessRadioMessage(void *pvParameters)
{
    MqttMessage msg;
    NodeData nodeData;
    for (;;)
    {
        if (xQueueReceive(qData, &nodeData, QUEUE_RECEIVE_DELAY))
        {
            debugPrint("taskProcessRadioMessage");
            msg.len = sizeof(nodeData.id) + sizeof(nodeData.RSSI) + nodeData.length;
            strlcpy(msg.topic, moduleSettings.hostname, sizeof(msg.topic));
            memcpy(msg.data, &nodeData, msg.len);

            if (xQueueSendToBack(qMqtt, &msg, QUEUE_RECEIVE_DELAY) != pdPASS)
                debugPrint("Failed add to MQTT queue");
        }
    }
}
