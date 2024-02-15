#include <Arduino.h>
#include <RFM69.h>
#include <eewl.h>
#include <Vcc.h>
#include <Cfg.h>
#include <NodeState.h>
#include <NodeData.h>
#include <StoragePackage.h>
#include <periphery.h>
#include <functions.h>

#define BUFFER_START 0x1
#define BUFFER_LEN 100

StoragePackage package = {1, 0};

RFM69 radio;
Vcc vcc;
EEWL storage(package, BUFFER_LEN, BUFFER_START);

volatile int32_t pulseCounter = 0;
volatile uint8_t staleCounter = 0;
volatile uint32_t lastPulseTime = 0;
volatile bool pulsePinState = HIGH;
volatile NodeState nodeState = NodeState::Ready;
uint8_t sleepCounter = 0;
uint32_t time = 0;
// NOTE: Digit 11 represents a msessage type, check "Radio payload formats (RFM69)" for more details
NodeData nodeData = {11};
uint8_t nodeSendErrors = 0;


ISR(WDT_vect)
{
  wdt_reset();
}

// ISR(PCINT1_vect)
// {
//   onPulse();
// }

void onPulse()
{
  uint32_t ms = millis();
  nodeState = NodeState::Pulse;
  staleCounter = 0;

  bool state = digitalRead(Cfg::pinPulse);
  if (state == pulsePinState)
    return;

  if (ms - lastPulseTime > 200 || lastPulseTime > ms)
  {
    lastPulseTime = ms;
    
    if (pulsePinState == LOW && state ==  HIGH)
    {
      pulseCounter += 1;
      nodeState = NodeState::PulseEnd;
    }

    pulsePinState = state; 
  }
}

void setup()
{
  wdt_enable(WDTO_4S);
  delay(250);
#ifdef NODE_DEBUG
  Serial.begin(9600);
  Serial.print(F("Node "));
  Serial.print(RADIO_NODE_ID);
  Serial.println(F(" starting..."));
#endif
#ifdef VCC_EEPROM
  vcc.loadCalibrationFromEEPROM(Cfg::addrVccCalibration);
#endif

  prepareUnusedPins(Cfg::ncPins, sizeof(Cfg::ncPins));
  pinMode(Cfg::pinPulse, INPUT_PULLUP);
  clearInterrupt1Flag();

  storage.begin();
  storage.get(package);
  pulseCounter = package.pulseCounter;
  pulsePinState = digitalRead(Cfg::pinPulse);

  Serial.println(pulseCounter);

  attachInterrupt(digitalPinToInterrupt(Cfg::pinPulse), onPulse, CHANGE);

  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef NODE_DEBUG
  if (radioState)
    Serial.println(F("RF69: init ok"));
  else
    Serial.println(F("RF69: error during init"));
#endif
#ifdef RADIO_HIGH_POWER
  radio.setHighPower(true);
#endif
  radio.encrypt(RADIO_ENCRYPTION_KEY);
  radio.setPowerLevel(RADIO_POWER_LEVEL);
  radio.sleep();

  turnModulesOff();
  wdt_reset();
}

void loop()
{
  wdt_reset();

  switch (nodeState)
  {
  case NodeState::Sleep:
    handleSleepState();
    break;
  case NodeState::StaleSleep:
    handleStaleSleepState();
    break;
  case NodeState::Pulse:
  case NodeState::PulseEnd:
    handlePulseState(lastPulseTime);
    break;
  case NodeState::Ready:
    handleReadyState(pulseCounter);
    break;
  default:
    break;
  }
}
