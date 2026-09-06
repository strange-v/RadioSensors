#pragma once

#include <TelemetryFrames.h>
#include <stddef.h>
#include <stdint.h>

#include "CommissioningService.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "ProvisioningButton.h"
#include "TelemetrySchedule.h"

namespace radiosensors {
namespace node {

template <typename Profile>
class ClimateNodeApplication {
public:
    ClimateNodeApplication(
        Profile& profile, NodeRadio& radio,
        CommissioningService& commissioning, LowPowerClock& clock,
        ProvisioningButton& button, const ClimateReportPolicy reportPolicy)
        : profile_(profile),
          radio_(radio),
          commissioning_(commissioning),
          clock_(clock),
          button_(button),
          reportPolicy_(reportPolicy),
          reportSchedule_(reportPolicy.intervalForMillivolts(0)) {}

    void begin() {
        profile_.begin();
        button_.begin();
        clock_.begin();
        radioReady_ = commissioning_.begin();
#if defined(NODE_DEBUG)
        Serial.print(F("radio: "));
        Serial.println(radioReady_ ? F("ready") : F("initialization failed"));
#endif
    }

    void runOnce() {
        const uint32_t now = clock_.nowMs();
        if (!radioReady_) {
            clock_.sleepUntilInterrupt();
            return;
        }

        if (!commissioning_.active()) {
            const bool requestedByButton = button_.consumePress();
            if (requestedByButton || !joinAttempted_ ||
                intervalElapsed(now, lastJoinAttempt_, kJoinRetryIntervalMs)) {
#if defined(NODE_DEBUG)
                if (requestedByButton) {
                    Serial.println(F("commissioning: requested by button"));
                }
#endif
                joinAttempted_ = true;
                lastJoinAttempt_ = now;
                commissioning_.advance();
            }
            clock_.sleepUntilInterrupt();
            return;
        }

        if (reportSchedule_.due(now) && radioRetry_.allowed(now)) {
#if defined(NODE_DEBUG)
            Serial.println(F("telemetry: measuring"));
#endif
            uint8_t frame[protocol::kTemperatureTelemetrySize];
            const size_t size = profile_.encodeTelemetry(frame, sizeof(frame));
            if (size != 0 && radio_.sendTelemetry(
                    commissioning_.config().gatewayId, frame,
                    static_cast<uint8_t>(size))) {
                const uint32_t nextInterval =
                    reportPolicy_.intervalForMillivolts(
                        profile_.supplyMillivolts());
                reportSchedule_.setInterval(nextInterval);
                reportSchedule_.transmissionSucceeded(now);
                radioRetry_.succeeded();
#if defined(NODE_DEBUG)
                Serial.print(F("telemetry: ack, vcc="));
                Serial.print(profile_.supplyMillivolts());
                Serial.print(F(" mV, next="));
                Serial.print(nextInterval / 1000UL);
                Serial.println(F(" s"));
#endif
            } else {
                radioRetry_.failed(now);
#if defined(NODE_DEBUG)
                Serial.println(F("telemetry: failed"));
#endif
            }
        }
        clock_.sleepUntilInterrupt();
    }

private:
    static constexpr uint32_t kJoinRetryIntervalMs = 5UL * 60UL * 1000UL;

    Profile& profile_;
    NodeRadio& radio_;
    CommissioningService& commissioning_;
    LowPowerClock& clock_;
    ProvisioningButton& button_;
    ClimateReportPolicy reportPolicy_;
    RollingKeepAlive reportSchedule_;
    RadioRetryBackoff radioRetry_;
    uint32_t lastJoinAttempt_ = 0;
    bool radioReady_ = false;
    bool joinAttempted_ = false;
};

}  // namespace node
}  // namespace radiosensors
