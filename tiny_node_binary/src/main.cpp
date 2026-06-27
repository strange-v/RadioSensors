#include <Arduino.h>
#include <RFM69.h>
#include <periphery.h>
#include <functions.h>

RFM69 radio(PIN_PA4, PIN_PA7);
NodeData data{RADIO_NODE_TYPE, 0, 0};
bool pinState = true;

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
  pinMode(PIN_REED_SWITCH, INPUT_PULLUP);

  bool radioState = radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
#ifdef RADIO_NODE_DEBUG_LED
  blink(radioState ? 1 : 3);
#endif

  radio.setHighPower(true);
  radio.encrypt(RADIO_ENCRYPTION_KEY);
  radio.setPowerLevel(RADIO_POWER_LEVEL);
  radio.sleep();

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
  pinMode(PIN_PA6, INPUT_PULLUP);
  bool state = digitalRead(PIN_PA6);

  if (state != pinState)
  {
    state = stablePinRead(PIN_PA6, 5);
    pinMode(PIN_PA6, OUTPUT);

    if (state != pinState)
    {
      pinState = state;
      data.state = state;
      data.vcc = readVcc();
      ADC0.CTRLA &= ~ADC_ENABLE_bm;

      transmitData(data);
    }
  }
  pinMode(PIN_PA6, OUTPUT);
#ifdef RADIO_NODE_DEBUG_PIN
  digitalWrite(RADIO_NODE_DEBUG_PIN, LOW);
#endif

  sleep_cpu();
}