#include <Arduino.h>
#include <RFM69.h>
#include <SparkFunHTU21D.h>
#include <Vcc.h>
#include <Cfg.h>
#include <NodeState.h>
#include <NodeData.h>
#include <periphery.h>
#include <functions.h>

RFM69 radio;
HTU21D htu;
Vcc vcc;

NodeState nodeState = NodeState::Ready;
uint8_t sleepCounter = 0;
uint16_t currentSendInterval;
uint32_t time;
NodeData nodeData;

ISR(WDT_vect)
{
  wdt_disable();
}

void setup()
{
  wdt_enable(WDTO_4S);

#ifdef NODE_DEBUG
  Serial.begin(9600);
  delay(1000);
  Serial.println(F("Starting..."));
#endif
  vcc.loadCalibrationFromEEPROM(Cfg::addrVccCalibration);

  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef NODE_DEBUG
  if (radioState)
    Serial.println(F("RF69: init ok"));
  else
    Serial.println(F("RF69: error during init"));
#endif
  radio.encrypt(RADIO_ENCRYPTION_KEY);
  radio.setPowerLevel(Cfg::radioPowerLevel);
  radio.sleep();

  htu.begin();

  float voltage = vcc.getValue();
  currentSendInterval = getSendInterval(voltage);

  prepareUnusedPins(Cfg::ncPins, sizeof(Cfg::ncPins));
  turnModulesOff();
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
    turnModulesOn();
    handleReadyState();
    turnModulesOff();
    break;
  default:
    break;
  }
}
