#include "OtaService.h"

#include <ArduinoOTA.h>

#include <atomic>

#include "DeviceIdentity.h"
#include "Diagnostics.h"
#include "EthernetService.h"
#include "OtaConfig.h"

namespace gateway::ota {
namespace {

constexpr uint16_t kOtaPort = 3232;
std::atomic<State> currentState{
    config::enabled ? State::WaitingForNetwork : State::Disabled};
std::atomic<uint8_t> currentProgress{0};
bool started = false;

void configureCallbacks() {
    ArduinoOTA.onStart([]() {
        currentProgress.store(0);
        currentState.store(State::Updating);
        Serial.println("OTA update started");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        const uint8_t percent = total == 0
            ? 0
            : static_cast<uint8_t>((static_cast<uint64_t>(progress) * 100U) / total);
        const uint8_t previous = currentProgress.exchange(percent);
        if (percent == 100 || percent >= previous + 10) {
            Serial.printf("OTA progress: %u%%\n", percent);
        }
        diagnostics::feedWatchdog();
    });

    ArduinoOTA.onEnd([]() {
        currentProgress.store(100);
        Serial.println("OTA update complete; rebooting");
    });

    ArduinoOTA.onError([](ota_error_t error) {
        currentState.store(State::Failed);
        Serial.printf("OTA error: %u\n", static_cast<unsigned>(error));
    });
}

}  // namespace

void begin() {
    if (!config::enabled) {
        Serial.println("OTA disabled: include/LocalSecrets.h is missing or has an empty password");
        return;
    }

    configureCallbacks();
    Serial.printf("OTA waiting for network as %s.local\n", identity::hostname());
}

void loop() {
    if (!config::enabled) {
        return;
    }

    if (!started) {
        if (!ethernet::hasIp()) {
            currentState.store(State::WaitingForNetwork);
            return;
        }

        ArduinoOTA.setPort(kOtaPort);
        ArduinoOTA.setHostname(identity::hostname());
        ArduinoOTA.setPassword(config::password);
        ArduinoOTA.begin();
        started = true;
        currentState.store(State::Ready);
        Serial.printf("OTA ready: %s.local:%u\n", identity::hostname(), kOtaPort);
    }

    ArduinoOTA.handle();
}

bool enabled() {
    return config::enabled;
}

State state() {
    return currentState.load();
}

const char* stateName() {
    switch (state()) {
        case State::Disabled:
            return "disabled";
        case State::WaitingForNetwork:
            return "waiting_for_network";
        case State::Ready:
            return "ready";
        case State::Updating:
            return "updating";
        case State::Failed:
            return "failed";
    }

    return "unknown";
}

uint8_t progressPercent() {
    return currentProgress.load();
}

}  // namespace gateway::ota

