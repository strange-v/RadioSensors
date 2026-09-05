#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

namespace radiosensors {
namespace node {

class Tmp112Sensor {
public:
    Tmp112Sensor(TwoWire& wire, uint8_t address)
        : wire_(wire), address_(address) {}

    void begin();
    bool readTemperature(int16_t& centiDegrees);

private:
    bool readRegister16(uint8_t registerAddress, uint16_t& value);
    bool writeRegister16(uint8_t registerAddress, uint16_t value);

    TwoWire& wire_;
    uint8_t address_;
};

}  // namespace node
}  // namespace radiosensors
