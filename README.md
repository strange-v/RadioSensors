# Wireless solar-powered sensor node
A solar-powered node that stores energy in a supercapacitor, wakes-up regularly, measures different parameters, and sends them to a gateway.

Please visit [hackaday.io](https://hackaday.io/project/175514-wireless-solar-powered-sensor-node) to find more information about the project.

# node
This is a firmware for the node. It uses [MiniCore](https://github.com/MCUdude/MiniCore) to be able to run ATmega328P with non-default quartz (4MHz).

In the platformio.ini you will find a few build flags:

#### NODE_DEBUG
Use it to print different debug information via UART0. Won't work properly when NODE_TRUE_SLEEP is define.

#### NODE_TRUE_SLEEP
Without this flag, the node emulates deep sleep using the delay function (useful for testing purposes).

#### NODE_PIN_DEBUG=N
Used to debug different work stages of the node triggering pin N.

#### VCC_EEPROM
An optional flag for the Vcc library that measures supply voltage. Using it, calibration value can be loaded from the EEPROM.

#### SENSOR_*
Defines what sensor is used in the node. Supported options for now:
- SENSOR_HTU21D
- SENSOR_BME280=0x76
Where 0x76 represents the I2C address.

#### RADIO_*
Options for RFM69 library. Node id, gateway id, network id. The encryption key is loaded by a different mechanism but also can be added here.

# gateway
Contains a gateway firmware. It supports a regular ESP32 and WT32-ETH01 module (with Ethernet). A few notes about it:
- heavily uses FreeRTOS functionality
- fully interrupt driven (requires the latest changes in the RFM69 library, see the [PR](https://github.com/LowPowerLab/RFM69/pull/181))

`pre:../../env.py` is used to securely add sensitive information (WiFi SSID, different passwords, radio encryption key).
