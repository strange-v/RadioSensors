#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <Arduino.h>
#include <LowPower.h>
#include <RFM69.h>
#include <SparkFunHTU21D.h>
#include <Vcc.h>
#include <Cfg.h>
#include <NodeState.h>
#include <NodeData.h>

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

RFM69 radio;
HTU21D htu;
Vcc vcc;

NodeState nodeState = NodeState::Ready;
uint8_t sleepCounter = 0;
uint16_t currentSendInterval;
uint32_t time;
NodeData nodeData;

#pragma region Functions

void disable()
{
  power_adc_disable();
  power_spi_disable();
  //power_timer0_disable();
  power_timer1_disable();
  power_timer2_disable();
  power_twi_disable();
  power_usart0_disable();
}

void enable()
{
  power_adc_enable();
  power_spi_enable();
  //power_timer0_enable();
  power_timer1_enable();
  power_timer2_enable();
  power_twi_enable();
  power_usart0_enable();
}


float getVcc()
{
  return vcc.getValue();
}

uint16_t getSendInterval(float vcc)
{
  uint16_t interval = vcc > Cfg::lowVoltageThreshold ? Cfg::sendIntervalHigh : Cfg::sendIntervalLow;

  return interval / Cfg::maxSleepTime;
}

void doSleep(period_t period)
{
  //wdt_disable();
#ifdef NODE_TRUE_SLEEP
  //LowPower.powerDown(period, ADC_OFF, BOD_OFF);
  // clear various "reset" flags
  MCUSR = 0;
  // allow changes, disable reset
  WDTCSR = bit(WDCE) | bit(WDE);
  // set interrupt mode and an interval
  WDTCSR = bit(WDIE) | bit(WDP3) | bit(WDP0); // set WDIE, and 8 seconds delay
  wdt_reset();                                // pat the dog

  cbi(ADCSRA, ADEN); // switch Analog to Digitalconverter OFF
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
  sleep_disable();
  sbi(ADCSRA, ADEN); // switch Analog to Digitalconverter ON
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
  //wdt_enable(WDTO_4S);
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

ISR(WDT_vect)
{
  wdt_disable();
}

void setup()
{
  //wdt_enable(WDTO_4S);

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
  radio.sleep();

  htu.begin();

  float vcc = getVcc();
  currentSendInterval = getSendInterval(vcc);

  for (uint8_t i = 0; i < sizeof(Cfg::ncPins); i++)
  {
    pinMode(Cfg::ncPins[i], OUTPUT);
    digitalWrite(Cfg::ncPins[i], LOW);
  }

  cbi(ADCSRA, ADEN);
  disable();
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
    enable();
    sbi(ADCSRA, ADEN);
    handleReadyState();
    cbi(ADCSRA, ADEN);
    disable();
    break;
  default:
    break;
  }
}