#include <Arduino.h>
#include <RFM69.h>
#include <Wire.h>
#include <limits.h>

namespace
{
constexpr uint8_t kTmp112TemperatureRegister = 0x00;
constexpr uint8_t kTmp112ConfigurationRegister = 0x01;
constexpr uint8_t kTelemetryTypeClimate = 3;
constexpr int16_t kSensorValueInvalid = INT16_MIN;

// Keep the legacy type-3 payload layout so the existing gateway/consumer can
// decode this test node. This project only populates temperature.
struct NodeData
{
  uint8_t type;
  int16_t temperature;
  int16_t humidity;
  int16_t vcc;
};

RFM69 radio(PIN_PA4, PIN_PA7, true);
uint32_t lastSendTime = 0;
bool firstMeasurement = true;

void printHexByte(uint8_t value)
{
  if (value < 0x10)
    Serial.write('0');
  Serial.print(value, HEX);
}

void printChipUid()
{
  volatile const uint8_t *serialNumber = &SIGROW_SERNUM0;

  Serial.print(F("Chip UID: "));
  for (uint8_t i = 0; i < 10; ++i)
    printHexByte(serialNumber[i]);
  Serial.println();
}

bool readRegister16(uint8_t registerAddress, uint16_t &value)
{
  Wire.beginTransmission(TMP112_I2C_ADDRESS);
  Wire.write(registerAddress);
  if (Wire.endTransmission(false) != 0)
    return false;

  if (Wire.requestFrom(static_cast<uint8_t>(TMP112_I2C_ADDRESS),
                       static_cast<uint8_t>(2)) != 2)
    return false;

  value = static_cast<uint16_t>(Wire.read()) << 8;
  value |= static_cast<uint8_t>(Wire.read());
  return true;
}

bool readTmp112Temperature(int16_t &temperatureCentiDegrees)
{
  uint16_t temperatureRegister;
  uint16_t configurationRegister;
  if (!readRegister16(kTmp112TemperatureRegister, temperatureRegister) ||
      !readRegister16(kTmp112ConfigurationRegister, configurationRegister))
    return false;

  // TMP112 supports 12-bit normal mode and 13-bit extended mode. Each LSB is
  // 0.0625 degrees Celsius in either mode.
  const bool extendedMode = (configurationRegister & 0x0010U) != 0;
  const uint8_t shift = extendedMode ? 3 : 4;
  const uint8_t bits = extendedMode ? 13 : 12;
  int16_t raw = static_cast<int16_t>(temperatureRegister >> shift);
  if ((raw & (1 << (bits - 1))) != 0)
    raw |= static_cast<int16_t>(~((1 << bits) - 1));

  // Convert raw * 0.0625 C to the legacy C * 100 representation, rounded
  // symmetrically to the nearest centi-degree.
  const int32_t scaled = static_cast<int32_t>(raw) * 625;
  temperatureCentiDegrees = static_cast<int16_t>(
      scaled >= 0 ? (scaled + 50) / 100 : (scaled - 50) / 100);
  return true;
}

uint16_t readVccMillivolts()
{
  VREF.CTRLA =
      (VREF.CTRLA & ~VREF_ADC0REFSEL_gm) | VREF_ADC0REFSEL_1V1_gc;
  ADC0.CTRLB = ADC_SAMPNUM_ACC64_gc;
  ADC0.CTRLC = ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV16_gc | ADC_SAMPCAP_bm;
  ADC0.CTRLD = ADC_INITDLY_DLY64_gc;
  ADC0.MUXPOS = ADC_MUXPOS_INTREF_gc;
  ADC0.CTRLA = ADC_ENABLE_bm;
  ADC0.COMMAND = ADC_STCONV_bm;
  while ((ADC0.COMMAND & ADC_STCONV_bm) != 0)
  {
  }

  const uint16_t result = ADC0.RES;
  ADC0.CTRLA &= ~ADC_ENABLE_bm;
  if (result == 0)
    return 0;

  return static_cast<uint16_t>((1024UL * 1100UL * 64UL) / result);
}

void sendTemperature()
{
  NodeData data{kTelemetryTypeClimate, kSensorValueInvalid,
                kSensorValueInvalid, kSensorValueInvalid};
  const bool sensorRead = readTmp112Temperature(data.temperature);
  data.vcc = static_cast<int16_t>(readVccMillivolts());

  Serial.print(F("TMP112: "));
  if (sensorRead)
  {
    Serial.print(data.temperature / 100.0F, 2);
    Serial.print(F(" C"));
  }
  else
  {
    Serial.print(F("read error"));
  }
  Serial.print(F(", VCC: "));
  Serial.print(data.vcc);
  Serial.print(F(" mV, radio: "));

  const bool sent =
      radio.sendWithRetry(RADIO_GATEWAY_ID, &data, sizeof(data), 3);
  if (sent)
  {
    Serial.print(F("OK, ACK RSSI: "));
    Serial.print(radio.RSSI);
    Serial.println(F(" dBm"));
  }
  else
  {
    Serial.println(F("error, ACK RSSI: unavailable"));
  }
  radio.sleep();
}
} // namespace

void setup()
{
  Serial.begin(9600);
  delay(50);
  Serial.println(F("node_test starting"));
  printChipUid();

  Wire.begin();

  const bool radioInitialized =
      radio.initialize(RF69_868MHZ, RADIO_NODE_ID, RADIO_NETWORK_ID);
  radio.setHighPower(true);
#ifdef RADIO_ENCRYPTION_KEY
  radio.encrypt(RADIO_ENCRYPTION_KEY);
#endif
  radio.setPowerLevel(RADIO_POWER_LEVEL);
  radio.sleep();

  Serial.print(F("RFM69 node "));
  Serial.print(RADIO_NODE_ID);
  Serial.println(radioInitialized ? F(": initialized") : F(": init error"));
}

void loop()
{
  const uint32_t now = millis();
  if (firstMeasurement || now - lastSendTime >= SEND_INTERVAL_MS)
  {
    firstMeasurement = false;
    lastSendTime = now;
    sendTemperature();
  }

  delay(10);
}
