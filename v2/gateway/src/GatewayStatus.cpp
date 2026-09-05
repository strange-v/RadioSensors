#include "GatewayStatus.h"

#include <atomic>

#include "RadioService.h"

namespace gateway::status {
namespace {

constexpr uint32_t kPairingWindowMs = 120000;
constexpr uint32_t kDebounceMs = 50;
constexpr uint8_t kBrightness = 20;

#if defined(GATEWAY_BOARD_WAVESHARE_S3_ETH)
constexpr int kBootButtonPin = 0;
constexpr int kRgbPin = 21;
#endif

std::atomic<Indication> current{Indication::Operational};
std::atomic<uint32_t> pairingEndsAt{0};
std::atomic<uint32_t> indicationEndsAt{0};
bool rawButtonPressed = false;
bool stableButtonPressed = false;
uint32_t rawButtonChangedAt = 0;

bool deadlineReached(const uint32_t now, const uint32_t deadline) {
    return deadline != 0 && static_cast<int32_t>(now - deadline) >= 0;
}

void setRgb(const uint8_t red, const uint8_t green, const uint8_t blue) {
#if defined(GATEWAY_BOARD_WAVESHARE_S3_ETH)
    rgbLedWrite(kRgbPin, red, green, blue);
#else
    (void)red;
    (void)green;
    (void)blue;
#endif
}

void togglePairing(const uint32_t now) {
    if (pairingActive()) {
        radio::requestProfile(radio::Profile::Operational);
        pairingEndsAt.store(0);
        current.store(Indication::Operational);
        Serial.println("Pairing window closed by BOOT button");
    } else {
        if (!radio::requestProfile(radio::Profile::Commissioning)) {
            current.store(Indication::Error);
            indicationEndsAt.store(now + 3000);
            Serial.println("Pairing unavailable: commissioning radio profile is not configured");
            return;
        }
        pairingEndsAt.store(now + kPairingWindowMs);
        current.store(Indication::Pairing);
        Serial.println("Pairing window opened by BOOT button for 120 seconds");
    }
    indicationEndsAt.store(0);
}

void render(const uint32_t now) {
    const uint32_t phase = now % 1000;
    switch (current.load()) {
        case Indication::Operational:
            setRgb(0, kBrightness, 0);
            break;
        case Indication::Pairing: {
            const uint32_t triangle = phase < 500 ? phase : 1000 - phase;
            const uint8_t level = static_cast<uint8_t>(8 + triangle * 64 / 500);
            setRgb(0, 0, level);
            break;
        }
        case Indication::PersistingNode:
            setRgb(phase < 300 ? kBrightness : 0, phase < 300 ? 24 : 0, 0);
            break;
        case Indication::AwaitingConfirm:
            setRgb(0, phase % 250 < 125 ? kBrightness : 0,
                   phase % 250 < 125 ? kBrightness : 0);
            break;
        case Indication::PairingSucceeded:
            setRgb(kBrightness, kBrightness, kBrightness);
            break;
        case Indication::Error:
            setRgb(phase % 200 < 100 ? kBrightness : 0, 0, 0);
            break;
        case Indication::ProfileConflict: {
            const bool on = phase < 100 || (phase >= 200 && phase < 300);
            setRgb(on ? kBrightness : 0, 0, on ? kBrightness : 0);
            break;
        }
    }
}

}  // namespace

void begin() {
#if defined(GATEWAY_BOARD_WAVESHARE_S3_ETH)
    pinMode(kBootButtonPin, INPUT_PULLUP);
    rawButtonPressed = digitalRead(kBootButtonPin) == LOW;
    stableButtonPressed = rawButtonPressed;
    rawButtonChangedAt = millis();
#endif
    render(millis());
}

void loop() {
    const uint32_t now = millis();
    if (deadlineReached(now, pairingEndsAt.load())) {
        radio::requestProfile(radio::Profile::Operational);
        pairingEndsAt.store(0);
        if (indicationEndsAt.load() == 0) current.store(Indication::Operational);
        Serial.println("Pairing window expired");
    }
    if (deadlineReached(now, indicationEndsAt.load())) {
        indicationEndsAt.store(0);
        current.store(pairingActive() ? Indication::Pairing : Indication::Operational);
    }

#if defined(GATEWAY_BOARD_WAVESHARE_S3_ETH)
    const bool pressed = digitalRead(kBootButtonPin) == LOW;
    if (pressed != rawButtonPressed) {
        rawButtonPressed = pressed;
        rawButtonChangedAt = now;
    }
    if (pressed != stableButtonPressed && now - rawButtonChangedAt >= kDebounceMs) {
        stableButtonPressed = pressed;
        if (stableButtonPressed) togglePairing(now);
    }
#endif
    render(now);
}

bool pairingActive() {
    const uint32_t deadline = pairingEndsAt.load();
    return deadline != 0 && static_cast<int32_t>(deadline - millis()) > 0;
}

uint32_t pairingRemainingSeconds() {
    if (!pairingActive()) return 0;
    return (pairingEndsAt.load() - millis() + 999) / 1000;
}

void closePairing() {
    pairingEndsAt.store(0);
    radio::requestProfile(radio::Profile::Operational);
    current.store(Indication::Operational);
    indicationEndsAt.store(0);
}

void indicate(const Indication indication, const uint32_t durationMs) {
    current.store(indication);
    indicationEndsAt.store(durationMs == 0 ? 0 : millis() + durationMs);
}

const char* indicationName() {
    switch (current.load()) {
        case Indication::Operational: return "operational";
        case Indication::Pairing: return "pairing";
        case Indication::PersistingNode: return "persisting_node";
        case Indication::AwaitingConfirm: return "awaiting_confirm";
        case Indication::PairingSucceeded: return "pairing_succeeded";
        case Indication::Error: return "error";
        case Indication::ProfileConflict: return "profile_conflict";
    }
    return "unknown";
}

}  // namespace gateway::status
