#pragma once

#include <stdint.h>

namespace radiosensors {
namespace node {

constexpr uint32_t kEventNodeKeepAliveMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kCounterMinimumReportMs = 60UL * 1000UL;

class ClimateReportPolicy {
public:
    static ClimateReportPolicy fixed(const uint32_t intervalMs) {
        return ClimateReportPolicy(intervalMs, intervalMs, 0);
    }

    static ClimateReportPolicy adaptive(
        const uint32_t chargedIntervalMs,
        const uint32_t lowChargeIntervalMs,
        const uint16_t lowChargeThresholdMv) {
        return ClimateReportPolicy(
            chargedIntervalMs, lowChargeIntervalMs, lowChargeThresholdMv);
    }

    uint32_t intervalForMillivolts(const uint16_t supplyMillivolts) const {
        return supplyMillivolts > lowChargeThresholdMv_
            ? chargedIntervalMs_
            : lowChargeIntervalMs_;
    }

private:
    ClimateReportPolicy(
        const uint32_t chargedIntervalMs,
        const uint32_t lowChargeIntervalMs,
        const uint16_t lowChargeThresholdMv)
        : chargedIntervalMs_(chargedIntervalMs),
          lowChargeIntervalMs_(lowChargeIntervalMs),
          lowChargeThresholdMv_(lowChargeThresholdMv) {}

    uint32_t chargedIntervalMs_;
    uint32_t lowChargeIntervalMs_;
    uint16_t lowChargeThresholdMv_;
};

inline bool intervalElapsed(
    const uint32_t now, const uint32_t since, const uint32_t interval) {
    return static_cast<uint32_t>(now - since) >= interval;
}

class RollingKeepAlive {
public:
    explicit RollingKeepAlive(const uint32_t intervalMs)
        : intervalMs_(intervalMs) {}

    bool due(const uint32_t now) const {
        return !hasSuccessfulTransmission_ ||
            intervalElapsed(now, lastSuccessfulTransmission_, intervalMs_);
    }

    void transmissionSucceeded(const uint32_t now) {
        lastSuccessfulTransmission_ = now;
        hasSuccessfulTransmission_ = true;
    }

    void setInterval(const uint32_t intervalMs) { intervalMs_ = intervalMs; }

private:
    uint32_t intervalMs_;
    uint32_t lastSuccessfulTransmission_ = 0;
    bool hasSuccessfulTransmission_ = false;
};

class BinaryReportSchedule {
public:
    BinaryReportSchedule() : keepAlive_(kEventNodeKeepAliveMs) {}

    void stateChanged() {
        stateChanged_ = true;
        urgent_ = true;
    }

    bool due(const uint32_t now) const {
        return stateChanged_ || keepAlive_.due(now);
    }

    // True once per state change: the first attempt to report a new state
    // ignores radio retry backoff, later retries of the same report do not.
    bool takeUrgent() {
        const bool urgent = urgent_;
        urgent_ = false;
        return urgent;
    }

    void transmissionSucceeded(const uint32_t now) {
        stateChanged_ = false;
        keepAlive_.transmissionSucceeded(now);
    }

private:
    RollingKeepAlive keepAlive_;
    bool stateChanged_ = false;
    bool urgent_ = false;
};

class CounterReportSchedule {
public:
    CounterReportSchedule()
        : keepAlive_(kEventNodeKeepAliveMs) {}

    void pulseRecorded() { countChanged_ = true; }

    bool due(const uint32_t now) const {
        return keepAlive_.due(now) ||
            (countChanged_ && hasSuccessfulTransmission_ &&
             intervalElapsed(
                 now, lastSuccessfulTransmission_, kCounterMinimumReportMs));
    }

    void transmissionSucceeded(const uint32_t now) {
        countChanged_ = false;
        lastSuccessfulTransmission_ = now;
        hasSuccessfulTransmission_ = true;
        keepAlive_.transmissionSucceeded(now);
    }

private:
    RollingKeepAlive keepAlive_;
    uint32_t lastSuccessfulTransmission_ = 0;
    bool hasSuccessfulTransmission_ = false;
    bool countChanged_ = false;
};

class RadioRetryBackoff {
public:
    bool allowed(const uint32_t now) const {
        return !waiting_ || intervalElapsed(now, failedAt_, delayMs_);
    }

    void failed(const uint32_t now) {
        static constexpr uint32_t delays[] = {
            60UL * 1000UL,
            5UL * 60UL * 1000UL,
            15UL * 60UL * 1000UL,
            60UL * 60UL * 1000UL,
        };
        if (failureLevel_ == 0xFF) {
            failureLevel_ = 0;
        } else if (failureLevel_ < 3) {
            ++failureLevel_;
        }
        failedAt_ = now;
        delayMs_ = delays[failureLevel_];
        waiting_ = true;
    }

    void succeeded() {
        failureLevel_ = 0xFF;
        waiting_ = false;
    }

private:
    uint32_t failedAt_ = 0;
    uint32_t delayMs_ = 0;
    uint8_t failureLevel_ = 0xFF;
    bool waiting_ = false;
};

// Limits command sessions that a telemetry acknowledgement starts. A gateway
// that keeps announcing a command it never records would otherwise cost a
// session per report: at most one per five minutes, and after one the gateway
// did not answer, the radio retry delays.
class HintedSessionPolicy {
public:
    static constexpr uint32_t kMinimumIntervalMs = 5UL * 60UL * 1000UL;

    bool allowed(const uint32_t now) const {
        return (!hasSession_ ||
                intervalElapsed(now, lastSession_, kMinimumIntervalMs)) &&
            unanswered_.allowed(now);
    }

    void finished(const uint32_t now, const bool answered) {
        lastSession_ = now;
        hasSession_ = true;
        if (answered) unanswered_.succeeded();
        else unanswered_.failed(now);
    }

private:
    RadioRetryBackoff unanswered_;
    uint32_t lastSession_ = 0;
    bool hasSession_ = false;
};

}  // namespace node
}  // namespace radiosensors
