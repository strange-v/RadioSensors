#pragma once

#include <avr/io.h>
#include <stddef.h>
#include <stdint.h>

#include "BatteryMonitor.h"
#include "CommissioningService.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "ProvisioningButton.h"
#include "SupplyVoltage.h"
#include "TelemetrySchedule.h"

namespace radiosensors {
namespace node {

// Runtime shared by every node image. A profile provides:
//
//   static constexpr uint16_t kProfileId;
//   static constexpr size_t kTelemetrySize;
//   void begin();
//   void poll(uint32_t now);  // every clock tick, also before commissioning
//   bool reportDue(uint32_t now) const;
//   bool takeUrgentReport();  // true once per event that must not wait for
//                             // radio retry backoff
//   size_t encodeTelemetry(uint16_t supplyMillivolts, uint8_t* output,
//                          size_t capacity);
//   void reportAcknowledged(uint32_t now, uint16_t supplyMillivolts);
//
// The contract is a template parameter, not an interface: avr-gcc keeps
// vtables in RAM, and each image contains exactly one profile.
template <typename Profile>
class NodeRuntime {
public:
    NodeRuntime(
        Profile& profile, NodeRadio& radio,
        CommissioningService& commissioning, LowPowerClock& clock,
        ProvisioningButton& button)
        : profile_(profile),
          radio_(radio),
          commissioning_(commissioning),
          clock_(clock),
          button_(button) {}

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
        const ButtonGesture gesture = button_.takeGesture();
        if (gesture == ButtonGesture::LongPress) resetNetwork();
        const uint32_t now = clock_.nowMs();
        profile_.poll(now);
        if (radioReady_) {
            if (commissioning_.active()) {
#if defined(NODE_DEBUG)
                if (gesture == ButtonGesture::ShortPress) {
                    Serial.println(
                        F("button: command sessions are not implemented"));
                }
#endif
                reportIfDue(now);
            } else {
                commissionIfDue(now, gesture == ButtonGesture::ShortPress);
            }
        }
        clock_.sleepUntilInterrupt();
    }

private:
    static constexpr uint32_t kJoinRetryIntervalMs = 5UL * 60UL * 1000UL;

    // USERROW and profile EEPROM, including the counter, survive. The restart
    // brings the node up unconfigured, on its factory commissioning profile.
    void resetNetwork() {
        if (!commissioning_.resetNetwork()) {
#if defined(NODE_DEBUG)
            Serial.println(
                F("button: network reset refused, factory credentials missing"));
#endif
            return;
        }
#if defined(NODE_DEBUG)
        Serial.println(F("button: network configuration erased, restarting"));
        Serial.flush();
#endif
        _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRE_bm);
        while (true) {}
    }

    void commissionIfDue(const uint32_t now, const bool requestedByButton) {
        if (!requestedByButton && joinAttempted_ &&
            !intervalElapsed(now, lastJoinAttempt_, kJoinRetryIntervalMs)) {
            return;
        }
#if defined(NODE_DEBUG)
        if (requestedByButton) {
            Serial.println(F("commissioning: requested by button"));
        }
#endif
        joinAttempted_ = true;
        lastJoinAttempt_ = now;
        commissioning_.advance();
    }

    void reportIfDue(const uint32_t now) {
        const bool urgent = profile_.takeUrgentReport();
        if (!profile_.reportDue(now) ||
            (!urgent && !radioRetry_.allowed(now))) {
            return;
        }
#if defined(NODE_DEBUG)
        Serial.println(F("telemetry: measuring"));
#endif
        const uint16_t supplyMillivolts =
            supplyVoltage_.report(battery_.readMillivolts());
        uint8_t frame[Profile::kTelemetrySize];
        const size_t size =
            profile_.encodeTelemetry(supplyMillivolts, frame, sizeof(frame));
        bool acknowledged = false;
        if (size != 0) {
            acknowledged = radio_.sendTelemetry(
                commissioning_.config().gatewayId, frame,
                static_cast<uint8_t>(size));
            supplyVoltage_.transmitted(battery_.readMillivolts());
        }
        if (acknowledged) {
            profile_.reportAcknowledged(now, supplyMillivolts);
            radioRetry_.succeeded();
#if defined(NODE_DEBUG)
            Serial.print(F("telemetry: ack, vcc="));
            Serial.print(supplyMillivolts);
            Serial.println(F(" mV"));
#endif
        } else {
            radioRetry_.failed(now);
#if defined(NODE_DEBUG)
            Serial.println(F("telemetry: failed"));
#endif
        }
    }

    Profile& profile_;
    NodeRadio& radio_;
    CommissioningService& commissioning_;
    LowPowerClock& clock_;
    ProvisioningButton& button_;
    BatteryMonitor battery_;
    LoadedSupplyVoltage supplyVoltage_;
    RadioRetryBackoff radioRetry_;
    uint32_t lastJoinAttempt_ = 0;
    bool radioReady_ = false;
    bool joinAttempted_ = false;
};

}  // namespace node
}  // namespace radiosensors
