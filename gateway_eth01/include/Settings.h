#ifndef SETTINGS_h
#define SETTINGS_h

#include <Arduino.h>
#include <EEPROM.h>

#define START_ADDR 0
#define SIGNATURE 0x123455F0


struct Settings
{
    uint32_t signature;

    char hostname[16];
};

Settings getSettings();
void saveSettings(Settings newSettings);
Settings _getDefaultSettings();

#endif