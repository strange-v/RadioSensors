#include <functions.h>

void handleSleepState()
{
  doSleep();

  sleepCounter++;

#ifdef NODE_DEBUG
  Serial.printf("%d of %d\n", sleepCounter, Cfg::sendIntervalSec / Cfg::sleepTimeSec);
  delay(100);
#endif

  bool state = digitalRead(Cfg::pinPulse);
  if (state == LOW)
  {
    if (staleCounter < Cfg::staleLimit)
    {
      staleCounter++;
    }
    else
    {
      nodeState = NodeState::StaleSleep;
      detachInterrupt(digitalPinToInterrupt(Cfg::pinPulse));
      pinMode(Cfg::pinPulse, INPUT);
    }

  }
#ifdef NODE_DEBUG
  Serial.printf("%s %d\n", state ? "HIGH" : "LOW", staleCounter);
#endif

  if (sleepCounter >= Cfg::sendIntervalSec / Cfg::sleepTimeSec)
  {
    sleepCounter = 0;
    nodeState = NodeState::Ready;
  }
}

void handleStaleSleepState()
{
  pinMode(Cfg::pinPulse, INPUT_PULLUP);
  bool state = digitalRead(Cfg::pinPulse);
  if (state == HIGH)
  {
    pulseCounter += 1;
    nodeState = NodeState::Ready;

    clearInterrupt1Flag();
    attachInterrupt(digitalPinToInterrupt(Cfg::pinPulse), onPulse, CHANGE);
    return;
  }

  pinMode(Cfg::pinPulse, INPUT);

  doSleep(true);

  sleepCounter++;

#ifdef NODE_DEBUG
  Serial.printf("stl %d of %d\n", sleepCounter / Cfg::sleepTimeSec, Cfg::sendIntervalSec / Cfg::sleepTimeSec);
  delay(100);
#endif

  if (sleepCounter / Cfg::sleepTimeSec >= Cfg::sendIntervalSec / Cfg::sleepTimeSec)
  {
    sleepCounter = 0;
    nodeState = NodeState::Ready;
  }
}

void handlePulseState(uint32_t lastPulseTime)
{
  uint32_t ms = millis();
  if (ms - lastPulseTime > 250 || lastPulseTime > ms)
  {
    nodeState = nodeState == NodeState::PulseEnd ? NodeState::Ready : NodeState::Sleep;
  }
}

void handleReadyState(int32_t pulseCounter)
{
  turnModulesOn();

  nodeData.counter = pulseCounter;
  if (package.pulseCounter != pulseCounter)
  {
    package.pulseCounter = nodeData.counter;
    package.pulsePinState = pulsePinState;
    storage.put(package);
  }
  nodeData.vcc = vcc.getValue_mV();
  turnAdcOff();

#ifdef NODE_DEBUG
  Serial.printf("C: %d, V: %d\r\n", nodeData.counter, nodeData.vcc);
  delay(100);
#endif

  transmitData(nodeData);
  
  radio.sleep();
  turnModulesOff();

  if (nodeState == NodeState::Ready)
    nodeState = NodeState::Sleep;

#ifdef NODE_DEBUG
  uint32_t t = millis();
  Serial.printf("Awake: %d ms\n", t > time ? t - time : UINT32_MAX - time + t);
#endif
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
  time = millis();
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
#endif
  }
  else
  {
    if (nodeSendErrors < 255)
      nodeSendErrors++;
#ifdef NODE_DEBUG
    Serial.println("ERR");
#endif
  }
  //TODO: Find out why it is required
  delay(1);
  return result;
}