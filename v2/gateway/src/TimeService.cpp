#include "TimeService.h"

#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>

#include "EthernetService.h"

namespace gateway::time_service {
namespace {

constexpr const char* kPrimaryNtpServer = "pool.ntp.org";
constexpr const char* kSecondaryNtpServer = "time.cloudflare.com";
constexpr time_t kMinimumValidUnixTime = 1577836800;  // 2020-01-01 UTC

std::atomic<State> currentState{State::WaitingForNetwork};
std::atomic<uint64_t> lastSyncMs{0};
bool started = false;

uint64_t currentUnixTimeMs() {
    timeval now{};
    gettimeofday(&now, nullptr);
    if (now.tv_sec < kMinimumValidUnixTime) return 0;
    return static_cast<uint64_t>(now.tv_sec) * 1000ULL +
        static_cast<uint64_t>(now.tv_usec / 1000);
}

void onTimeSynchronized(timeval*) {
    const uint64_t synchronizedAt = currentUnixTimeMs();
    if (synchronizedAt == 0) return;
    lastSyncMs.store(synchronizedAt, std::memory_order_release);
    currentState.store(State::Synchronized, std::memory_order_release);
}

}  // namespace

void begin() {
    esp_sntp_set_time_sync_notification_cb(onTimeSynchronized);
}

void loop() {
    if (!started && ethernet::hasIp()) {
        configTime(0, 0, kPrimaryNtpServer, kSecondaryNtpServer);
        started = true;
        currentState.store(State::Synchronizing, std::memory_order_release);
        Serial.println("Time synchronization started");
    }
}

State state() {
    return currentState.load(std::memory_order_acquire);
}

const char* stateName() {
    switch (state()) {
        case State::WaitingForNetwork:
            return "waiting_for_network";
        case State::Synchronizing:
            return "synchronizing";
        case State::Synchronized:
            return "synchronized";
    }
    return "unknown";
}

uint64_t unixTimeMs() {
    if (state() != State::Synchronized) return 0;
    return currentUnixTimeMs();
}

uint64_t lastSyncUnixMs() {
    return lastSyncMs.load(std::memory_order_acquire);
}

}  // namespace gateway::time_service
