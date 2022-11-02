#include <Arduino.h>
#include <RFM69.h>
#include <Vcc.h>
#include <Cfg.h>
#include <NodeState.h>
#include <NodeData.h>
#include <periphery.h>
#include <functions.h>

RFM69 radio;
Vcc vcc;
#ifdef SENSOR_HTU21D
#include <SparkFunHTU21D.h>
HTU21D htu;
#endif
#ifdef SENSOR_BME280
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;
#endif

NodeState nodeState = NodeState::Ready;
uint8_t sleepCounter = 0;
uint16_t currentSendInterval = 0;
uint32_t time = 0;
//NOTE: Digit 4 represents a msessage type, check "Radio payload formats (RFM69)" for more details
NodeData nodeData = {4};
uint32_t nodeUptime = 0;
uint8_t nodeSendErrors = 0;

ISR(WDT_vect)
{
  wdt_disable();
}

void setup()
{
  wdt_enable(WDTO_4S);
  prepareUnusedPins(Cfg::ncPins, sizeof(Cfg::ncPins));

#ifdef NODE_DEBUG
  Serial.begin(9600);
  delay(1000);
  Serial.print(F("Node "));
  Serial.print(RADIO_NODE_ID);
  Serial.println(F(" starting..."));
#endif
#ifdef VCC_EEPROM
  vcc.loadCalibrationFromEEPROM(Cfg::addrVccCalibration);
#endif

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

#ifdef SENSOR_HTU21D
  htu.begin();
#endif
#ifdef SENSOR_BME280
  bool bmeState = bme.begin(SENSOR_BME280);
  if (bmeState)
  {
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
  }
#ifdef NODE_DEBUG
  if (bmeState)
    Serial.println(F("BME: init ok"));
  else
    Serial.println(F("BME: error during init"));
#endif
#endif

  float voltage = vcc.getValue();
  currentSendInterval = getSendInterval(voltage);

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
