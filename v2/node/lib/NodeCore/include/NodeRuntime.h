#pragma once

#include <CommandSessionFrames.h>
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
                const bool commandPending = reportIfDue(now);
                if (gesture == ButtonGesture::ShortPress) {
                    runCommandSession();
                } else if (commandPending && hintedSessions_.allowed(now)) {
                    hintedSessions_.finished(now, runCommandSession());
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

    // Returns whether the acknowledgement announced a pending command.
    bool reportIfDue(const uint32_t now) {
        const bool urgent = profile_.takeUrgentReport();
        if (!profile_.reportDue(now) ||
            (!urgent && !radioRetry_.allowed(now))) {
            return false;
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
        bool commandPending = false;
        if (size != 0) {
            acknowledged = radio_.sendTelemetry(
                commissioning_.config().gatewayId, frame,
                static_cast<uint8_t>(size), commandPending);
            supplyVoltage_.transmitted(battery_.readMillivolts());
        }
        if (acknowledged) {
            profile_.reportAcknowledged(now, supplyMillivolts);
            radioRetry_.succeeded();
#if defined(NODE_DEBUG)
            Serial.print(F("telemetry: ack, vcc="));
            Serial.print(supplyMillivolts);
            Serial.println(commandPending ? F(" mV, command pending") : F(" mV"));
#endif
        } else {
            radioRetry_.failed(now);
#if defined(NODE_DEBUG)
            Serial.println(F("telemetry: failed"));
#endif
        }
        return commandPending;
    }

    // Returns whether the gateway answered: with No command, or with a
    // command that was then applied and answered.
    bool runCommandSession() {
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
                Serial.println(F("command: none pending"));
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
        Serial.println(F("command: gateway did not answer"));
#endif
        radio_.sleep();
        return false;
    }

    void answerCommand(const uint8_t gatewayId, const protocol::Command& command) {
        protocol::CommandResult result{};
        result.sessionNonce = command.sessionNonce;
        result.commandId = command.commandId;
        result.status = protocol::CommandStatus::Unsupported;
        bool powerStored = false;
        if (command.type ==
            static_cast<uint8_t>(protocol::CommandType::SetRadioPower)) {
            if (protocol::validCommandArguments(
                    command.type, command.arguments, command.argumentSize)) {
                result.status = commissioning_.storeRadioPower(
                    command.commandId, command.arguments[0]);
                powerStored = result.status == protocol::CommandStatus::Applied;
            } else {
                result.status = protocol::CommandStatus::InvalidArgument;
            }
        } else {
            profile_.applyCommand(command, result);
        }
        uint8_t bytes[protocol::kMaxCommandResultSize];
        const bool sent =
            protocol::encodeCommandResult(result, bytes, sizeof(bytes)) ==
                protocol::CommandSessionCodecStatus::Ok &&
            radio_.sendAcknowledged(
                gatewayId, bytes,
                static_cast<uint8_t>(protocol::commandResultFrameSize(result)));
        if (powerStored) commissioning_.applyRadioProfile();
        radio_.sleep();
#if defined(NODE_DEBUG)
        Serial.print(F("command: id="));
        Serial.print(command.commandId);
        Serial.print(F(" type="));
        Serial.print(command.type);
        Serial.print(F(" status="));
        Serial.print(static_cast<uint8_t>(result.status));
        Serial.println(sent ? F(" ack") : F(" no ack"));
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
    uint32_t lastJoinAttempt_ = 0;
    bool radioReady_ = false;
    bool joinAttempted_ = false;
};

}  // namespace node
}  // namespace radiosensors
