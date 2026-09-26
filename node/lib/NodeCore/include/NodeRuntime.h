#pragma once

#include <CommandSessionFrames.h>
#include <TelemetryFrames.h>
#include <avr/io.h>
#include <stddef.h>
#include <stdint.h>

#include "BatteryMonitor.h"
#include "CommissioningService.h"
#include "DebugLog.h"
#include "LowPowerClock.h"
#include "NodeRadio.h"
#include "ProvisioningButton.h"
#include "RadioPowerState.h"
#include "SupplyVoltage.h"
#include "TelemetrySchedule.h"
#include "WatchdogWindow.h"

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
//   size_t encodeTelemetry(const protocol::TelemetryPrefix& prefix,
//                          uint8_t* output, size_t capacity);
//   void reportAcknowledged(uint32_t now, uint16_t supplyMillivolts);
//   void applyCommand(const protocol::Command& command,
//                     protocol::CommandResult& result);
//       // a profile command; `result.status` arrives as Unsupported
//   void commissioned();  // forget command IDs recorded by the profile
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
#if defined(NODE_DEBUG)
        // The core moves the reset flags to GPIOR0 before setup().
        if ((GPIOR0 & RSTCTRL_WDRF_bm) != 0) debugLine(F("rst wdt"));
#endif
        {
            WatchdogWindow watchdog;
            profile_.begin();
            button_.begin();
            clock_.begin();
            radioStart_ = commissioning_.begin();
        }
#if defined(NODE_DEBUG)
        debugLine(radioStart_ == StartStatus::Ready ? F("rf ok") : F("rf fail"));
#endif
    }

    void runOnce() {
        const ButtonGesture gesture = button_.takeGesture();
        if (gesture == ButtonGesture::LongPress) resetNetwork();
        const uint32_t now = clock_.nowMs();
        profile_.poll(now);
        if (radioStart_ == StartStatus::RadioFailed &&
            intervalElapsed(now, 0, kRadioRestartDelayMs)) {  // since boot
#if defined(NODE_DEBUG)
            debugLine(F("rst rf"));
#endif
            restart();
        }
        if (radioStart_ == StartStatus::Ready) {
            if (commissioning_.active()) {
                const bool commandPending = reportIfDue(now);
                if (gesture == ButtonGesture::ShortPress) {
                    runCommandSession();
                    reportIfDue(now);
                } else if (commandPending && hintedSessions_.allowed(now)) {
                    hintedSessions_.finished(now, runCommandSession());
                    reportIfDue(now);
                }
            } else {
                commissionIfDue(now, gesture == ButtonGesture::ShortPress);
                if (commissioning_.active()) profile_.commissioned();
            }
        }
        clock_.sleepUntilInterrupt();
    }

private:
    static constexpr uint32_t kJoinRetryIntervalMs = 5UL * 60UL * 1000UL;
    static constexpr uint32_t kSessionWindowMs = 250;
    static constexpr uint8_t kSessionAttempts = 3;
    // A radio that failed to start gets a fresh boot; the delay bounds how
    // often a dead module costs one.
    static constexpr uint32_t kRadioRestartDelayMs = 5UL * 60UL * 1000UL;

    // USERROW and profile EEPROM, including the counter, survive. The restart
    // brings the node up unconfigured, on its factory commissioning profile.
    void resetNetwork() {
        if (!commissioning_.resetNetwork()) {
#if defined(NODE_DEBUG)
            debugLine(F("rst no fcred"));
#endif
            return;
        }
#if defined(NODE_DEBUG)
        debugLine(F("rst"));
#endif
        restart();
    }

    static void restart() {
#if defined(NODE_DEBUG)
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
        if (requestedByButton) debugLine(F("join btn"));
#endif
        WatchdogWindow watchdog;
        joinAttempted_ = true;
        lastJoinAttempt_ = now;
        commissioning_.advance();
    }

    // Returns whether the acknowledgement announced a pending command.
    bool reportIfDue(const uint32_t now) {
        const bool urgent = profile_.takeUrgentReport();
        if (!profile_.reportDue(now) ||
            (!urgent && !radioRetry_.allowed(now))) {
            return false;
        }
        WatchdogWindow watchdog;
        const protocol::TelemetryPrefix prefix{
            supplyVoltage_.report(battery_.readMillivolts()),
            protocol::encodeRadioState(powerLevel_, radioFallback_),
            downlinkRssi_};
        uint8_t frame[Profile::kTelemetrySize];
        const size_t size =
            profile_.encodeTelemetry(prefix, frame, sizeof(frame));
        bool acknowledged = false;
        protocol::TelemetryAck ack{false, false, 0};
        int8_t ackRssi = protocol::kNoDownlinkRssi;
        if (size != 0) {
            acknowledged = radio_.sendTelemetry(
                commissioning_.config().gatewayId, frame,
                static_cast<uint8_t>(size), ack, ackRssi);
            supplyVoltage_.transmitted(battery_.readMillivolts());
        }
        if (acknowledged) {
            downlinkRssi_ = ackRssi;
            profile_.reportAcknowledged(now, prefix.supplyMillivolts);
            radioRetry_.succeeded();
            unacknowledgedReports_ = 0;
            applyPower(afterAcknowledged(
                powerLevel_, radioFallback_, ack.hasPowerTarget,
                ack.powerTarget, NODE_RADIO_MAX_POWER_LEVEL));
#if defined(NODE_DEBUG)
            // The RSSI is in [-128, 0] dBm, -128 when not measured: print
            // its magnitude.
            Serial.print(F("tx ok mv="));
            Serial.print(prefix.supplyMillivolts);
            debugValue(F(" rssi=-"), static_cast<uint8_t>(-downlinkRssi_));
#endif
        } else {
            radioRetry_.failed(now);
#if defined(NODE_DEBUG)
            debugLine(F("tx fail"));
#endif
            if (unacknowledgedReports_ < UINT8_MAX) ++unacknowledgedReports_;
            applyPower(afterUnacknowledged(
                powerLevel_, radioFallback_, unacknowledgedReports_,
                NODE_RADIO_MAX_POWER_LEVEL));
        }
        return acknowledged && ack.commandPending;
    }

    void applyPower(const PowerDecision decision) {
        if (!decision.change) return;
        powerLevel_ = decision.level;
        radioFallback_ = decision.fallback;
        radio_.setPowerLevel(decision.level);
#if defined(NODE_DEBUG)
        debugValue(decision.fallback ? F("pwr fb ") : F("pwr "), decision.level);
#endif
    }

    // Returns whether the gateway answered: with No command, or with a
    // command that was then applied and answered.
    bool runCommandSession() {
        WatchdogWindow watchdog;
        const uint8_t gatewayId = commissioning_.config().gatewayId;
        const uint32_t nonce = commissioning_.createNonce();
        uint8_t ready[protocol::kCommandReadySize];
        protocol::encodeCommandReady(nonce, ready, sizeof(ready));
        uint8_t frame[protocol::kMaxCommandSize];
        for (uint8_t attempt = 0; attempt < kSessionAttempts; ++attempt) {
            radio_.send(gatewayId, ready, sizeof(ready));
            const uint8_t size = radio_.receiveFrame(
                kSessionWindowMs, gatewayId, frame, sizeof(frame));
            uint32_t echoed = 0;
            if (protocol::decodeNoCommand(frame, size, echoed) ==
                    protocol::CommandSessionCodecStatus::Ok &&
                echoed == nonce) {
#if defined(NODE_DEBUG)
                debugLine(F("cmd none"));
#endif
                radio_.sleep();
                return true;
            }
            protocol::Command command{};
            if (protocol::decodeCommand(frame, size, command) ==
                    protocol::CommandSessionCodecStatus::Ok &&
                command.sessionNonce == nonce) {
                answerCommand(gatewayId, command);
                return true;
            }
        }
#if defined(NODE_DEBUG)
        debugLine(F("cmd timeout"));
#endif
        radio_.sleep();
        return false;
    }

    void answerCommand(const uint8_t gatewayId, const protocol::Command& command) {
        protocol::CommandResult result{};
        result.sessionNonce = command.sessionNonce;
        result.commandId = command.commandId;
        result.status = protocol::CommandStatus::Unsupported;
        profile_.applyCommand(command, result);
        uint8_t bytes[protocol::kMaxCommandResultSize];
        const bool sent =
            protocol::encodeCommandResult(result, bytes, sizeof(bytes)) ==
                protocol::CommandSessionCodecStatus::Ok &&
            radio_.sendAcknowledged(
                gatewayId, bytes,
                static_cast<uint8_t>(protocol::commandResultFrameSize(result)));
        radio_.sleep();
#if defined(NODE_DEBUG)
        Serial.print(sent ? F("cmd ack id=") : F("cmd noack id="));
        Serial.print(command.commandId);
        Serial.print(F(" t="));
        Serial.print(command.type);
        debugValue(F(" s="), static_cast<uint8_t>(result.status));
#else
        (void)sent;
#endif
    }

    Profile& profile_;
    NodeRadio& radio_;
    CommissioningService& commissioning_;
    LowPowerClock& clock_;
    ProvisioningButton& button_;
    BatteryMonitor battery_;
    LoadedSupplyVoltage supplyVoltage_;
    RadioRetryBackoff radioRetry_;
    HintedSessionPolicy hintedSessions_;
    int8_t downlinkRssi_ = protocol::kNoDownlinkRssi;
    // Held only in RAM: a restart begins at the ceiling and the gateway's next
    // acknowledgement restores its target, so no level change wears the
    // EEPROM.
    uint8_t powerLevel_ = NODE_RADIO_MAX_POWER_LEVEL;
    bool radioFallback_ = false;
    uint8_t unacknowledgedReports_ = 0;
    uint32_t lastJoinAttempt_ = 0;
    StartStatus radioStart_ = StartStatus::RadioFailed;
    bool joinAttempted_ = false;
};

}  // namespace node
}  // namespace radiosensors
