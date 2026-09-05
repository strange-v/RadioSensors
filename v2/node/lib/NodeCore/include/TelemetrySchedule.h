#pragma once

#include <stdint.h>

namespace radiosensors {
namespace node {

constexpr uint32_t kEventNodeKeepAliveMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kCounterMinimumReportMs = 60UL * 1000UL;

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

private:
    uint32_t intervalMs_;
    uint32_t lastSuccessfulTransmission_ = 0;
    bool hasSuccessfulTransmission_ = false;
};

class DoorReportSchedule {
public:
    DoorReportSchedule() : keepAlive_(kEventNodeKeepAliveMs) {}

    void stateChanged() { stateChanged_ = true; }

    bool due(const uint32_t now) const {
        return stateChanged_ || keepAlive_.due(now);
    }

    void transmissionSucceeded(const uint32_t now) {
        stateChanged_ = false;
        keepAlive_.transmissionSucceeded(now);
    }

private:
    RollingKeepAlive keepAlive_;
    bool stateChanged_ = false;
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

}  // namespace node
}  // namespace radiosensors
