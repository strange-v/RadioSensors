#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

namespace radiosensors {
namespace node {

class Sht40Sensor {
public:
    Sht40Sensor(TwoWire& wire, uint8_t address)
        : wire_(wire), address_(address) {}

    void begin();
    bool read(int16_t& temperatureCentiDegrees,
              uint16_t& humidityCentiPercent);

private:
    static uint8_t crc8(const uint8_t* data, uint8_t size);

    TwoWire& wire_;
    uint8_t address_;
};

}  // namespace node
}  // namespace radiosensors
