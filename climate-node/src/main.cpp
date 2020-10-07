#include <avr/wdt.h>
#include <Arduino.h>
#include <LowPower.h>
#include <RFM69.h>
#include <SparkFunHTU21D.h>
#include <Vcc.h>
#include <Cfg.h>
#include <NodeState.h>
#include <NodeData.h>

RFM69 radio;
HTU21D htu;

NodeState nodeState = NodeState::Ready;
uint8_t sleepCounter = 0;
uint16_t currentSendInterval;
uint32_t time;
NodeData nodeData;

#pragma region Functions
float getVcc()
{
  pinMode(Cfg::pinDivider, OUTPUT);
  digitalWrite(Cfg::pinDivider, LOW);
  delay(10);

  float vcc = Vcc::getValue();
  int value = analogRead(Cfg::pinVcc);
  float vout = (value * vcc) / 1024.0;
  float vin = vout / (Cfg::r2 / (Cfg::r1 + Cfg::r2));

#ifdef NODE_DEBUG
  if (vin < 1)
    vin = 6.66;
#endif

  pinMode(Cfg::pinDivider, INPUT);

  return vin;
}

uint16_t getSendInterval(float vcc)
{
  uint16_t interval = vcc > Cfg::lowVoltageThreshold ? Cfg::sendIntervalHigh : Cfg::sendIntervalLow;

  return interval / Cfg::maxSleepTime;
}

void doSleep(period_t period)
{
  wdt_disable();
#ifdef NODE_TRUE_SLEEP
  LowPower.powerDown(period, ADC_OFF, BOD_OFF);
#else
  switch (period)
  {
  case SLEEP_15MS:
    delay(15);
    break;
  case SLEEP_30MS:
    delay(30);
    break;
  case SLEEP_60MS:
    delay(60);
    break;
  case SLEEP_120MS:
    delay(120);
    break;
  case SLEEP_250MS:
    delay(250);
    break;
  case SLEEP_500MS:
    delay(500);
    break;
  case SLEEP_1S:
    delay(1000);
    break;
  case SLEEP_2S:
    delay(2000);
    break;
  case SLEEP_4S:
    delay(4000);
    break;
  case SLEEP_8S:
    delay(8000);
    break;
  }
#endif
  time = millis();
  wdt_enable(WDTO_4S);
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
  doSleep(SLEEP_8S);
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
  digitalWrite(3, sleepCounter % 2 ? HIGH : LOW);
  Serial.println(sleepCounter);
#endif

  if (sleepCounter >= currentSendInterval)
  {
    sleepCounter = 0;
    nodeState = NodeState::Ready;
  }
  else
  {
    nodeData.uptime += Cfg::maxSleepTime;
    sleepCounter++;
    doSleep(SLEEP_8S);
  }
}

void handleReadyState()
{
  //float vcc = Vcc::getValue();
  float vcc = getVcc();
  float temperature = htu.readTemperature();
  float humidity = htu.readHumidity();

  nodeData.temperature = temperature;
  nodeData.humidity = humidity;
  nodeData.vcc = vcc;

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
  transmitData(nodeData);

  nodeSleep(vcc);
}
#pragma endregion

void setup()
{
  wdt_enable(WDTO_4S);

#ifdef NODE_DEBUG
  Serial.begin(9600);
  delay(1000);
  Serial.println(F("Starting..."));

  pinMode(3, OUTPUT);
#endif

  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef NODE_DEBUG
  if (radioState)
    Serial.println(F("RF69: init ok"));
  else
    Serial.println(F("RF69: error during init"));
#endif
  radio.encrypt(RADIO_ENCRYPTION_KEY);
  radio.sleep();

  htu.begin();

  //float vcc = Vcc::getValue();
  float vcc = getVcc();
  currentSendInterval = getSendInterval(vcc);
}

void loop()
{
  wdt_reset();

  switch (nodeState)
  {
  case NodeState::Sleep:
    handleSleepState();
    break;
  case NodeState::Ready:
    handleReadyState();
    break;
  default:
    break;
  }
}