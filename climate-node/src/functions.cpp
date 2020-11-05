#include <functions.h>

uint16_t getSendInterval(float vcc)
{
  uint16_t interval = vcc > Cfg::lowVoltageThreshold
                          ? Cfg::sendIntervalHigh
                          : Cfg::sendIntervalLow;

  return interval / Cfg::maxSleepTime;
}

void doSleep()
{
#ifdef NODE_TRUE_SLEEP
  sleepMcu();
#else
  delay(8000);
#endif
  wdt_enable(WDTO_4S);
  time = millis();
}

void nodeSleep(float vcc)
{
  radio.sleep();

  uint32_t max = 4294967295;
  uint32_t t = millis();

  nodeData.uptime += t > time
                         ? (t - time) / 1000
                         : (max - time + t) / 1000;
#ifdef NODE_DEBUG
  uint32_t awake = t > time
                       ? t - time
                       : max - time + t;
  Serial.printf("Node was awake: %d ms\n", awake);
#endif
  currentSendInterval = getSendInterval(vcc);
  nodeState = NodeState::Sleep;
  doSleep();
}

void transmitData(NodeData data)
{
  if (radio.sendWithRetry(RADIO_GATEWAY_ID, &data, sizeof(data)))
  {
    nodeData.errors = 0;
#ifdef NODE_DEBUG
    Serial.println(F("Sent ok"));
#endif
  }
  else
  {
    nodeData.errors++;
#ifdef NODE_DEBUG
    Serial.println(F("Sent err"));
#endif
  }
  delay(1); //ToDo: Research why it is required VF
}

void handleSleepState()
{
#ifdef NODE_DEBUG
  Serial.println(sleepCounter);
#endif

  nodeData.uptime += Cfg::maxSleepTime;
  sleepCounter++;

  if (sleepCounter >= currentSendInterval)
  {
    sleepCounter = 0;
    nodeState = NodeState::Ready;
  }
  else
  {
    doSleep();
  }
}

void handleReadyState()
{
#ifdef NODE_PIN_DEBUG
  digitalWrite(4, HIGH);
#endif
  float voltage = vcc.getValue();
  turnAdcOff();
#ifdef NODE_PIN_DEBUG
  digitalWrite(4, LOW);
#endif

#ifdef NODE_PIN_DEBUG
  digitalWrite(4, HIGH);
#endif
  float temperature = htu.readTemperature();
  float humidity = htu.readHumidity();
  power_twi_disable();
#ifdef NODE_PIN_DEBUG
  digitalWrite(4, LOW);
#endif

  nodeData.temperature = temperature;
  nodeData.humidity = humidity;
  nodeData.vcc = voltage;

#ifdef NODE_DEBUG
  Serial.print("T:");
  Serial.println(nodeData.temperature, 2);
  Serial.print("H:");
  Serial.println(nodeData.humidity, 2);
  Serial.print("V:");
  Serial.println(nodeData.vcc, 2);
  Serial.print("U:");
  Serial.println(nodeData.uptime);
#endif
#ifdef NODE_PIN_DEBUG
  digitalWrite(4, HIGH);
#endif
  transmitData(nodeData);
#ifdef NODE_PIN_DEBUG
  digitalWrite(4, LOW);
#endif

  nodeSleep(voltage);
}
