#include <Arduino.h>
#include <RFM69.h>
#include <Cfg.h>
#include <NodeAData.h>

RFM69 radio;

void proccessNodeA()
{
  NodeAData data;
  memcpy(&data, &radio.DATA, radio.DATALEN);

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
}

void setup()
{
#ifdef GW_DEBUG
  Serial.begin(115200);
  Serial.println(F("Starting..."));
#endif
    bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef GW_DEBUG
    Serial.println(radioState ? F("RF69: init ok") : F("RF69: error during init"));
#endif
  radio.encrypt(RADIO_ENCRYPTION_KEY);
}

void loop()
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