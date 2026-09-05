#pragma once

#include <TelemetryFrames.h>
#include <stddef.h>
#include <stdint.h>

#include "CommissioningService.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "TelemetrySchedule.h"

namespace radiosensors {
namespace node {

template <typename Profile>
class ClimateNodeApplication {
public:
    ClimateNodeApplication(
        Profile& profile, NodeRadio& radio,
        CommissioningService& commissioning, LowPowerClock& clock,
        const uint32_t reportIntervalMs)
        : profile_(profile),
          radio_(radio),
          commissioning_(commissioning),
          clock_(clock),
          reportSchedule_(reportIntervalMs) {}

    void begin() {
        profile_.begin();
        clock_.begin();
        radioReady_ = commissioning_.begin();
    }

    void runOnce() {
        const uint32_t now = clock_.nowMs();
        if (!radioReady_) {
            clock_.sleepUntilInterrupt();
            return;
        }

        if (!commissioning_.active()) {
            if (!joinAttempted_ ||
                intervalElapsed(now, lastJoinAttempt_, kJoinRetryIntervalMs)) {
                joinAttempted_ = true;
                lastJoinAttempt_ = now;
                commissioning_.advance();
            }
            clock_.sleepUntilInterrupt();
            return;
        }

        if (reportSchedule_.due(now) && radioRetry_.allowed(now)) {
            uint8_t frame[protocol::kTemperatureTelemetrySize];
            const size_t size = profile_.encodeTelemetry(frame, sizeof(frame));
            if (size != 0 && radio_.sendTelemetry(
                    commissioning_.config().gatewayId, frame,
                    static_cast<uint8_t>(size))) {
                reportSchedule_.transmissionSucceeded(now);
                radioRetry_.succeeded();
            } else {
                radioRetry_.failed(now);
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
    RollingKeepAlive reportSchedule_;
    RadioRetryBackoff radioRetry_;
    uint32_t lastJoinAttempt_ = 0;
    bool radioReady_ = false;
    bool joinAttempted_ = false;
};

}  // namespace node
}  // namespace radiosensors
