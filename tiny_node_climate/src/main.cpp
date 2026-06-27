#include <Arduino.h>
#include <RFM69.h>
#include "Adafruit_SHT31.h"
#include <periphery.h>
#include <functions.h>

RFM69 radio(PIN_PA4, PIN_PA7);
Adafruit_SHT31 sht = Adafruit_SHT31();
NodeData data{RADIO_NODE_TYPE, 0, 0};
bool pinState = true;
uint16_t batteryVoltage = 0;

ISR(RTC_PIT_vect)
{
  RTC.PITINTFLAGS = RTC_PI_bm;
}

void setup()
{
  pinMode(PIN_PA0, INPUT_PULLUP);
  pinMode(PIN_PA5, INPUT_PULLUP);
  pinMode(PIN_PA6, INPUT_PULLUP);
  pinMode(PIN_PA7, INPUT);
  pinMode(PIN_PB0, INPUT_PULLUP);
  pinMode(PIN_PB1, INPUT_PULLUP);
  pinMode(PIN_PB2, INPUT_PULLUP);
  pinMode(PIN_PB3, INPUT_PULLUP);

#ifdef RADIO_NODE_DEBUG_LED
  pinMode(RADIO_NODE_DEBUG_LED, OUTPUT);
  digitalWrite(RADIO_NODE_DEBUG_LED, LOW);
#endif
#ifdef RADIO_NODE_DEBUG_PIN
  pinMode(RADIO_NODE_DEBUG_PIN, OUTPUT);
  digitalWrite(RADIO_NODE_DEBUG_PIN, LOW);
#endif

  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef RADIO_NODE_DEBUG_LED
  blink(radioState ? 1 : 3);
#endif

  radio.setHighPower(true);
  radio.encrypt(RADIO_ENCRYPTION_KEY);
  radio.setPowerLevel(RADIO_POWER_LEVEL);
  radio.sleep();

  sht.begin(0x44);

  ADC0.CTRLA &= ~ADC_ENABLE_bm;  // Disable ADC0
  TCA0.SPLIT.CTRLA &= ~TCA_SPLIT_ENABLE_bm;  // Disable TCA0

  initRTC();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sei();
}

void loop()
{
#ifdef RADIO_NODE_DEBUG_PIN
  digitalWrite(RADIO_NODE_DEBUG_PIN, HIGH);
#endif
  float temperature = 0;
  float humidity = 0;
  if (sht.readBoth(&temperature, &humidity))
  {
    data.temperature = static_cast<int16_t>(temperature * 100);
    data.humidity = static_cast<int16_t>(humidity * 100);
  }
  uint16_t vcc = readVcc();
  data.vcc = (vcc < batteryVoltage || batteryVoltage == 0) ? vcc : batteryVoltage;

  transmitData(data);

  batteryVoltage = readVcc();
  ADC0.CTRLA &= ~ADC_ENABLE_bm;
#ifdef RADIO_NODE_DEBUG_PIN
  digitalWrite(RADIO_NODE_DEBUG_PIN, LOW);
#endif

  sleep_cpu();
}