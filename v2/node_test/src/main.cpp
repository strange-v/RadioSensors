#include <Arduino.h>
#include <CommissioningFrames.h>
#include <EEPROM.h>
#include <JoinRequest.h>
#include <ProfileIds.h>
#include <RFM69.h>
#include <Wire.h>
#include <limits.h>
#include <string.h>

namespace
{
using namespace radiosensors::protocol;

constexpr uint8_t kRadioChipSelect = PIN_PA4;
constexpr uint8_t kRadioInterrupt = PIN_PA7;
constexpr uint8_t kCommissioningNodeId = 0;
constexpr uint8_t kCommissioningNetworkId = 0;
constexpr uint16_t kProfileId = profileIdValue(ProfileId::Temperature);
constexpr FirmwareVersion kFirmwareVersion{0, 1, 0};
constexpr uint32_t kJoinRetryMs = 5000;
constexpr uint32_t kAcceptWindowMs = 2500;
constexpr uint32_t kConfirmRetryMs = 1000;
constexpr uint8_t kConfirmAttempts = 10;
constexpr uint8_t kTmp112TemperatureRegister = 0x00;
constexpr uint8_t kTmp112ConfigurationRegister = 0x01;
constexpr int16_t kSensorValueInvalid = INT16_MIN;

constexpr uint32_t kConfigMagic = 0x324E5352UL;
constexpr uint8_t kConfigSchema = 1;
constexpr uint8_t kStateProvisional = 1;
constexpr uint8_t kStateActive = 2;
constexpr uint8_t kConfigSize = 38;
constexpr uint8_t kConfigSlotCount = 2;

#ifndef RADIO_COMMISSIONING_KEY
#error "RADIO_COMMISSIONING_KEY must be an exactly 16-byte string."
#endif
static_assert(sizeof(RADIO_COMMISSIONING_KEY) == 17,
              "RADIO_COMMISSIONING_KEY must be exactly 16 bytes.");

struct StoredConfig
{
  uint32_t generation;
  uint8_t state;
  uint8_t nodeId;
  uint8_t gatewayId;
  uint8_t networkId;
  uint8_t powerLevel;
  uint8_t installationKey[kInstallationKeySize];
  uint32_t requestNonce;
};

RFM69 radio(kRadioChipSelect, kRadioInterrupt, true);
uint8_t deviceUid[kDeviceUidSize];
StoredConfig config{};
uint8_t currentSlot = 0;
uint32_t lastSendTime = 0;
bool firstMeasurement = true;

uint32_t crc32(const uint8_t *data, size_t size)
{
  uint32_t crc = 0xFFFFFFFFUL;
  while (size-- != 0)
  {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1U)));
  }
  return ~crc;
}

void encodeConfig(const StoredConfig &value, uint8_t *bytes)
{
  memset(bytes, 0, kConfigSize);
  writeUint32Le(bytes, kConfigMagic);
  bytes[4] = kConfigSchema;
  writeUint32Le(bytes + 5, value.generation);
  bytes[9] = value.state;
  bytes[10] = value.nodeId;
  bytes[11] = value.gatewayId;
  bytes[12] = value.networkId;
  bytes[13] = value.powerLevel;
  memcpy(bytes + 14, value.installationKey, kInstallationKeySize);
  writeUint32Le(bytes + 30, value.requestNonce);
  writeUint32Le(bytes + 34, crc32(bytes, 34));
}

bool decodeConfig(const uint8_t *bytes, StoredConfig &value)
{
  if (readUint32Le(bytes) != kConfigMagic || bytes[4] != kConfigSchema ||
      readUint32Le(bytes + 34) != crc32(bytes, 34))
    return false;
  if ((bytes[9] != kStateProvisional && bytes[9] != kStateActive) ||
      bytes[10] == 0 || bytes[10] == bytes[11] || bytes[10] == 255 ||
      bytes[11] == 0 || bytes[11] == 255)
    return false;
  value.generation = readUint32Le(bytes + 5);
  value.state = bytes[9];
  value.nodeId = bytes[10];
  value.gatewayId = bytes[11];
  value.networkId = bytes[12];
  value.powerLevel = bytes[13];
  memcpy(value.installationKey, bytes + 14, kInstallationKeySize);
  value.requestNonce = readUint32Le(bytes + 30);
  return true;
}

bool loadConfig()
{
  bool found = false;
  for (uint8_t slot = 0; slot < kConfigSlotCount; ++slot)
  {
    uint8_t bytes[kConfigSize];
    for (uint8_t i = 0; i < kConfigSize; ++i)
      bytes[i] = EEPROM.read(slot * kConfigSize + i);
    StoredConfig candidate{};
    if (decodeConfig(bytes, candidate) &&
        (!found || static_cast<int32_t>(candidate.generation - config.generation) > 0))
    {
      config = candidate;
      currentSlot = slot;
      found = true;
    }
  }
  return found;
}

void saveConfig()
{
  const uint8_t nextSlot = static_cast<uint8_t>((currentSlot + 1) % kConfigSlotCount);
  ++config.generation;
  uint8_t bytes[kConfigSize];
  encodeConfig(config, bytes);
  for (uint8_t i = 0; i < kConfigSize; ++i)
    EEPROM.update(nextSlot * kConfigSize + i, bytes[i]);
  currentSlot = nextSlot;
}

void printHexByte(uint8_t value)
{
  if (value < 0x10) Serial.write('0');
  Serial.print(value, HEX);
}

void readChipUid()
{
  volatile const uint8_t *serialNumber = &SIGROW_SERNUM0;
  Serial.print(F("Chip UID: "));
  for (uint8_t i = 0; i < kDeviceUidSize; ++i)
  {
    deviceUid[i] = serialNumber[i];
    printHexByte(deviceUid[i]);
  }
  Serial.println();
}

uint32_t createNonce()
{
  uint32_t value = micros() ^ 0xA5C31F27UL;
  for (uint8_t i = 0; i < kDeviceUidSize; ++i)
    value = (value ^ deviceUid[i]) * 16777619UL;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  return value;
}

void applyEncryptionKey(const uint8_t *key)
{
  char keyBuffer[kInstallationKeySize + 1];
  memcpy(keyBuffer, key, kInstallationKeySize);
  keyBuffer[kInstallationKeySize] = '\0';
  radio.encrypt(keyBuffer);
}

void useCommissioningProfile()
{
  radio.setAddress(kCommissioningNodeId);
  radio.setNetwork(kCommissioningNetworkId);
  radio.encrypt(RADIO_COMMISSIONING_KEY);
  radio.setPowerLevel(RADIO_POWER_LEVEL);
}

void useOperationalProfile()
{
  radio.setAddress(config.nodeId);
  radio.setNetwork(config.networkId);
  applyEncryptionKey(config.installationKey);
  radio.setPowerLevel(config.powerLevel);
}

bool receiveFrame(uint32_t timeoutMs, uint8_t expectedSender, uint8_t *output,
                  uint8_t expectedSize)
{
  const uint32_t started = millis();
  radio.receiveDone();
  while (millis() - started < timeoutMs)
  {
    if (radio.receiveDone())
    {
      if (radio.SENDERID == expectedSender && radio.DATALEN == expectedSize)
      {
        memcpy(output, radio.DATA, expectedSize);
        return true;
      }
      radio.receiveDone();
    }
  }
  return false;
}

bool awaitJoinComplete()
{
  JoinConfirm confirm{};
  memcpy(confirm.deviceUid, deviceUid, kDeviceUidSize);
  confirm.requestNonce = config.requestNonce;
  uint8_t confirmBytes[kJoinConfirmSize];
  encodeJoinConfirm(confirm, confirmBytes, sizeof(confirmBytes));
  for (uint8_t attempt = 0; attempt < kConfirmAttempts; ++attempt)
  {
    radio.send(config.gatewayId, confirmBytes, sizeof(confirmBytes), false);
    uint8_t completeBytes[kJoinCompleteSize];
    if (!receiveFrame(kConfirmRetryMs, config.gatewayId, completeBytes,
                      sizeof(completeBytes)))
      continue;
    JoinComplete complete{};
    if (decodeJoinComplete(completeBytes, sizeof(completeBytes), complete) ==
            CommissioningCodecStatus::Ok &&
        memcmp(complete.deviceUid, deviceUid, kDeviceUidSize) == 0 &&
        complete.requestNonce == config.requestNonce)
    {
      config.state = kStateActive;
      saveConfig();
      radio.sleep();
      Serial.println(F("Commissioning complete"));
      return true;
    }
  }
  radio.sleep();
  return false;
}

bool commission()
{
  useCommissioningProfile();
  JoinRequest request{};
  memcpy(request.deviceUid, deviceUid, kDeviceUidSize);
  request.profileId = kProfileId;
  request.firmware = kFirmwareVersion;
  request.requestNonce = createNonce();
  uint8_t requestBytes[kJoinRequestSize];
  encodeJoinRequest(request, requestBytes, sizeof(requestBytes));
  Serial.println(F("Sending JOIN_REQUEST"));
  radio.send(RADIO_GATEWAY_ID, requestBytes, sizeof(requestBytes), false);
  uint8_t acceptBytes[kJoinAcceptSize];
  if (!receiveFrame(kAcceptWindowMs, RADIO_GATEWAY_ID, acceptBytes,
                    sizeof(acceptBytes)))
  {
    radio.sleep();
    return false;
  }
  JoinAccept accept{};
  if (decodeJoinAccept(acceptBytes, sizeof(acceptBytes), accept) !=
          CommissioningCodecStatus::Ok ||
      memcmp(accept.deviceUid, deviceUid, kDeviceUidSize) != 0 ||
      accept.requestNonce != request.requestNonce ||
      accept.gatewayNodeId != RADIO_GATEWAY_ID || accept.installationKey[0] == 0)
  {
    radio.sleep();
    Serial.println(F("Rejected JOIN_ACCEPT"));
    return false;
  }
  config.state = kStateProvisional;
  config.nodeId = accept.assignedNodeId;
  config.gatewayId = accept.gatewayNodeId;
  config.networkId = accept.networkId;
  config.powerLevel = RADIO_POWER_LEVEL;
  memcpy(config.installationKey, accept.installationKey, kInstallationKeySize);
  config.requestNonce = accept.requestNonce;
  saveConfig();
  useOperationalProfile();
  Serial.print(F("Accepted node ID "));
  Serial.println(config.nodeId);
  return awaitJoinComplete();
}

bool readRegister16(uint8_t registerAddress, uint16_t &value)
{
  Wire.beginTransmission(TMP112_I2C_ADDRESS);
  Wire.write(registerAddress);
  if (Wire.endTransmission(false) != 0) return false;
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
  const bool extendedMode = (configurationRegister & 0x0010U) != 0;
  const uint8_t shift = extendedMode ? 3 : 4;
  const uint8_t bits = extendedMode ? 13 : 12;
  int16_t raw = static_cast<int16_t>(temperatureRegister >> shift);
  if ((raw & (1 << (bits - 1))) != 0)
    raw |= static_cast<int16_t>(~((1 << bits) - 1));
  const int32_t scaled = static_cast<int32_t>(raw) * 625;
  temperatureCentiDegrees = static_cast<int16_t>(
      scaled >= 0 ? (scaled + 50) / 100 : (scaled - 50) / 100);
  return true;
}

uint16_t readVccMillivolts()
{
  VREF.CTRLA = (VREF.CTRLA & ~VREF_ADC0REFSEL_gm) | VREF_ADC0REFSEL_1V1_gc;
  ADC0.CTRLB = ADC_SAMPNUM_ACC64_gc;
  ADC0.CTRLC = ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV16_gc | ADC_SAMPCAP_bm;
  ADC0.CTRLD = ADC_INITDLY_DLY64_gc;
  ADC0.MUXPOS = ADC_MUXPOS_INTREF_gc;
  ADC0.CTRLA = ADC_ENABLE_bm;
  ADC0.COMMAND = ADC_STCONV_bm;
  while ((ADC0.COMMAND & ADC_STCONV_bm) != 0) {}
  const uint16_t result = ADC0.RES;
  ADC0.CTRLA &= ~ADC_ENABLE_bm;
  return result == 0 ? 0 : static_cast<uint16_t>((1024UL * 1100UL * 64UL) / result);
}

void sendTemperature()
{
  int16_t temperature = kSensorValueInvalid;
  readTmp112Temperature(temperature);
  const uint16_t vcc = readVccMillivolts();
  uint8_t frame[5];
  frame[0] = encodeHeader(FrameKind::Telemetry);
  writeUint16Le(frame + 1, static_cast<uint16_t>(temperature));
  writeUint16Le(frame + 3, vcc);
  radio.send(config.gatewayId, frame, sizeof(frame), false);
  radio.sleep();
  Serial.print(F("Telemetry temp="));
  Serial.print(temperature / 100.0F, 2);
  Serial.print(F("C vcc="));
  Serial.print(vcc);
  Serial.println(F("mV sent"));
}
} // namespace

void setup()
{
  Serial.begin(9600);
  delay(50);
  Serial.println(F("RadioSensors v2 TMP112 node"));
  readChipUid();
  Wire.begin();
  const bool provisioned = loadConfig();
  const uint8_t initialNodeId = provisioned ? config.nodeId : kCommissioningNodeId;
  const uint8_t initialNetworkId = provisioned ? config.networkId : kCommissioningNetworkId;
  const bool initialized = radio.initialize(RF69_868MHZ, initialNodeId, initialNetworkId);
  radio.setHighPower(true);
  if (provisioned) useOperationalProfile();
  else useCommissioningProfile();
  radio.sleep();
  Serial.println(initialized ? F("RFM69 initialized") : F("RFM69 init error"));
  if (provisioned && config.state == kStateProvisional)
  {
    Serial.println(F("Resuming JOIN_CONFIRM"));
    awaitJoinComplete();
  }
}

void loop()
{
  if (config.state != kStateActive)
  {
    commission();
    delay(kJoinRetryMs);
    return;
  }
  const uint32_t now = millis();
  if (firstMeasurement || now - lastSendTime >= SEND_INTERVAL_MS)
  {
    firstMeasurement = false;
    lastSendTime = now;
    sendTemperature();
  }
  delay(10);
}
