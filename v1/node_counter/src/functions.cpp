#include <functions.h>

void handlePulse()
{
  turnModulesOn();

  if (package.pulseCounter != nodeData.counter)
  {
    package.pulseCounter = nodeData.counter;
    storage.put(package);
  }
  nodeData.vcc = vcc.getValue_mV();
  turnAdcOff();

#ifdef NODE_DEBUG
  Serial.printf("C: %d, V: %d\r\n", nodeData.counter, nodeData.vcc);
  Serial.flush();
#endif

  transmitData(nodeData);
  
  radio.sleep();
  turnModulesOff();
}

void doSleep(bool shortPeriod)
{
#ifdef NODE_TRUE_SLEEP
  sleepMcu(shortPeriod);
#else
  wdt_disable();
  if (shortPeriod)
    delay(1024);
  else
    delay(8000);
#endif
  wdt_enable(WDTO_4S);
}

bool transmitData(NodeData data)
{
  bool result = false;
  uint8_t retries = nodeSendErrors > Cfg::maxRetryErrors ? 0 : 2;
  if (radio.sendWithRetry(RADIO_GATEWAY_ID, &data, sizeof(data), retries))
  {
    result = true;
    nodeSendErrors = 0;
#ifdef NODE_DEBUG
    Serial.println("OK");
    Serial.flush();
#endif
  }
  else
  {
    if (nodeSendErrors < 255)
      nodeSendErrors++;
#ifdef NODE_DEBUG
    Serial.println("ERR");
    Serial.flush();
#endif
  }
  //TODO: Find out why it is required
  delay(1);
  return result;
}