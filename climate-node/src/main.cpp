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
uint32_t time;
NodeData nodeData;

#pragma region Functions
uint16_t getSendInterval(float vcc)
{
  uint16_t interval = vcc > Cfg::lowVoltageThreshold ? Cfg::sendIntervalHigh : Cfg::sendIntervalLow;

  return interval / Cfg::maxSleepTime;
}

void doSleep(period_t period)
{
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

  wdt_enable(WDTO_8S);
}

void nodeSleep()
{
  nodeState = NodeState::Sleep;
  radio.sleep();

  uint32_t max = 4294967295;
  uint32_t t = millis();

  nodeData.uptime += t > time
    ? (t - time) / 1000
    : (max - time + t) / 1000;

  doSleep(SLEEP_8S);
}

void transmitData(NodeData data)
{
	if(radio.sendWithRetry(RADIO_GATEWAY_ID, &data, sizeof(data)))
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
    Serial.println(F("Sent err..."));
  #endif
  }
}

void handleSleepState(float vcc)
{
#ifdef NODE_DEBUG  
  digitalWrite(3, sleepCounter % 2 ? HIGH : LOW);
  Serial.println(sleepCounter);
#endif

  if (sleepCounter >= getSendInterval(vcc))
  {
    sleepCounter = 0;
    nodeState = NodeState::Ready;
    time = millis();
  }
  else
  {
    nodeData.uptime += Cfg::maxSleepTime;
    sleepCounter++;
    doSleep(SLEEP_8S);
  }
}

void handleReadyState(float vcc)
{
  float temperature = htu.readTemperature();
  float humidity = htu.readHumidity();

  nodeData.temperature = temperature;
  nodeData.humidity = humidity;
  nodeData.vcc = vcc;

  transmitData(nodeData);
  Serial.print("T:");
  Serial.println(nodeData.temperature, 2);
  Serial.print("H:");
  Serial.println(nodeData.humidity, 2);
  Serial.print("V:");
  Serial.println(nodeData.vcc, 2);
  Serial.print("U:");
  Serial.println(nodeData.uptime);

  nodeSleep();
}
#pragma endregion

void setup()
{
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
}

void loop()
{
  float vcc = Vcc::getValue();

  switch (nodeState)
  {
  case NodeState::Sleep:
    handleSleepState(vcc);
    break;
  case NodeState::Ready:
    handleReadyState(vcc);
    break;
  default:
    break;
  }
}