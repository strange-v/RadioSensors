# Wireless solar-powered sensor node
A solar-powered node that stores energy in a supercapacitor, wakes-up regularly, measures different parameters, and sends them to a gateway.

Please visit [hackaday.io](https://hackaday.io/project/175514-wireless-solar-powered-sensor-node) to find more information about the project.

# climate-node
This is a firmware for the node. It uses [MiniCore](https://github.com/MCUdude/MiniCore) to be able to run ATmega328P with non-default quartz (4MHz).

In the platformio.ini you will find a few build flags:

#### NODE_DEBUG
Use it to print different debug information via UART0. Won't work properly when NODE_TRUE_SLEEP is define.

#### NODE_TRUE_SLEEP
Without this flag, the node emulates deep sleep using the delay function (useful for testing purposes).

#### NODE_PIN_DEBUG
When defined, the node outputs HIGH on pin 4 during reading sensors and transmitting data, and HIGH on pin 3 when active (not sleeping).

#### VCC_EEPROM
An optional flag for the Vcc library that measures supply voltage. Using it, calibration value can be loaded from the EEPROM.

#### RADIO_*
Options for RFM69 library. Node id, gateway id, network id, and an encryption key.

# gateway
It is a gateway firmware, simple and not 100% reliable. I have a lot of plans how it could look in the future, but currently i use it as-is to test the node. Gateway is based on ESP32.

To build this firmware _gateway/include/Cfg.example.h_ should be renamed to _Cfg.h_.
