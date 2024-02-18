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

StoragePackage package = {0};

RFM69 radio;
Vcc vcc;
EEWL storage(package, BUFFER_LEN, BUFFER_START);

int pulsePinState = HIGH;
// NOTE: Digit 11 represents a msessage type, check "Radio payload formats (RFM69)" for more details
NodeData nodeData = {11};
uint8_t nodeSendErrors = 0;
uint32_t sleepCounter = 0;


ISR(WDT_vect)
{
  wdt_reset();
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
  Serial.flush();
#endif
#ifdef VCC_EEPROM
  vcc.loadCalibrationFromEEPROM(Cfg::addrVccCalibration);
#endif

  prepareUnusedPins(Cfg::ncPins, sizeof(Cfg::ncPins));
  pinMode(Cfg::pinPulse, INPUT_PULLUP);

  storage.begin();
  storage.get(package);
  nodeData.counter = package.pulseCounter;
  pulsePinState = digitalRead(Cfg::pinPulse);
  uint32_t time = 0;

  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef NODE_DEBUG
  if (radioState)
    Serial.println(F("RF69: init ok"));
  else
    Serial.println(F("RF69: error during init"));
  Serial.flush();
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

  uint32_t ms = millis();
  pinMode(Cfg::pinPulse, INPUT_PULLUP);
  bool state = stablePinRead(Cfg::pinPulse, Cfg::numOfReadings);
  if (pulsePinState != state)
  {
    if (state == HIGH)
    {
      // End of pulse
      nodeData.counter++;
      handlePulse();
    }
    else if (sleepCounter % Cfg::sendIntervalSec == 0)
    {
      handlePulse();
    }

    pulsePinState = state;
  }
  pinMode(Cfg::pinPulse, INPUT);
#ifdef NODE_DEBUG
  Serial.printf("awake: %d ms\n", millis() - ms);
  Serial.flush();
#endif

  doSleep(true);

  sleepCounter++;
}
