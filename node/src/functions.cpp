#include <functions.h>

uint16_t getSendInterval(int16_t vcc)
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
  wdt_disable();
  delay(8000);
#endif
  wdt_enable(WDTO_4S);
  time = millis();
}

void nodeSleep(int16_t vcc)
{
  radio.sleep();

  uint32_t max = 4294967295;
  uint32_t t = millis();

  nodeUptime += t > time
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
    nodeSendErrors = 0;
#ifdef NODE_DEBUG
    Serial.println(F("Sent ok"));
#endif
  }
  else
  {
    nodeSendErrors++;
#ifdef NODE_DEBUG
    Serial.println(F("Sent err"));
#endif
  }
  delay(1); //ToDo: Research why it is required VF
}

void handleSleepState()
{
  nodeUptime += Cfg::maxSleepTime;
  sleepCounter++;

#ifdef NODE_DEBUG
  Serial.printf("%d of %d\n", sleepCounter, currentSendInterval);
#endif

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
  int16_t voltage = vcc.getValue_mV();
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

  nodeData.temperature = static_cast<int16_t>(temperature * 100);
  nodeData.humidity = static_cast<int16_t>(humidity * 100);
  nodeData.vcc = voltage;

#ifdef NODE_DEBUG
  Serial.print("T:");
  Serial.println(nodeData.temperature);
  Serial.print("H:");
  Serial.println(nodeData.humidity);
  Serial.print("V:");
  Serial.println(nodeData.vcc);
  Serial.print("U:");
  Serial.println(nodeUptime);
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
