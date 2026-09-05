#include "RadioService.h"

#include <RFM69.h>
#include <JoinRequest.h>
#include <SPI.h>

#include <atomic>

#include "RadioConfig.h"

namespace gateway::radio {
namespace {

constexpr uint8_t kExpectedVersion = 0x24;
constexpr uint32_t kTaskStackSize = 4096;
// Radio FIFO service is the gateway's highest-priority application work.
// Keep it above AsyncTCP (priority 10) while leaving the upper FreeRTOS
// priorities available to the ESP-IDF system tasks.
constexpr UBaseType_t kTaskPriority = 11;
constexpr BaseType_t kTaskCore = 1;
constexpr UBaseType_t kRxQueueDepth = 16;
constexpr UBaseType_t kCommandQueueDepth = 8;

SPIClass radioSpi(config::spiHost);
RFM69 rfm69(
    config::chipSelect,
    config::interrupt,
    config::highPower,
    &radioSpi);

std::atomic<State> currentState{State::Stopped};
std::atomic<Profile> currentProfile{Profile::Operational};
std::atomic<uint8_t> currentNetworkId{config::networkId};
TaskHandle_t radioTaskHandle = nullptr;
QueueHandle_t interruptQueue = nullptr;
QueueHandle_t receivedFrameQueue = nullptr;
QueueHandle_t commandQueue = nullptr;
QueueSetHandle_t radioQueueSet = nullptr;

enum class CommandKind : uint8_t { SetProfile, Send };

struct RadioCommand {
    CommandKind kind;
    Profile profile;
    uint16_t targetId;
    uint8_t data[kMaxPayloadSize];
    uint8_t size;
    bool requestAck;
    bool switchAfterSend;
};

struct TaskStats {
    uint32_t interrupts = 0;
    uint32_t packets = 0;
    uint32_t bytes = 0;
    uint32_t emptyWakeups = 0;
    uint32_t ackRequestsIgnored = 0;
    uint32_t v2Frames = 0;
    uint32_t v2TelemetryFrames = 0;
    uint32_t emptyApplicationFrames = 0;
    uint32_t unsupportedProtocolVersions = 0;
    uint32_t unsupportedFrameKinds = 0;
    uint32_t rxFramesQueued = 0;
    uint32_t rxFramesDropped = 0;
    uint32_t commandsQueued = 0;
    uint32_t commandsDropped = 0;
    uint32_t commandsProcessed = 0;
    uint32_t lastPacketMs = 0;
    uint16_t lastSenderId = 0;
    int16_t lastRssi = 0;
};

TaskStats taskStats;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t detectedVersion = 0;
uint32_t configuredFrequencyHz = 0;
uint32_t configuredBitRate = 0;

void IRAM_ATTR onRadioInterrupt() {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    const uint8_t signal = 1;
    if (interruptQueue != nullptr) {
        xQueueOverwriteFromISR(interruptQueue, &signal, &higherPriorityTaskWoken);
    }
    if (higherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void drainReceivedFrame() {
    portENTER_CRITICAL(&statsMux);
    ++taskStats.interrupts;
    portEXIT_CRITICAL(&statsMux);
    if (!rfm69.receiveDone()) {
        portENTER_CRITICAL(&statsMux);
        ++taskStats.emptyWakeups;
        portEXIT_CRITICAL(&statsMux);
        return;
    }

        const uint8_t dataLength = rfm69.DATALEN;
        const uint16_t senderId = rfm69.SENDERID;
        const int16_t rssi = rfm69.RSSI;
        const bool ackRequested = rfm69.ACKRequested();
        const uint32_t receivedAtMs = millis();
        radiosensors::protocol::FrameView frame{};
        const radiosensors::protocol::DecodeStatus decodeStatus =
            radiosensors::protocol::decodeFrame(
                rfm69.DATA,
                dataLength,
                frame);

        ReceivedFrame received{};
        received.size = dataLength;
        received.senderId = senderId;
        received.rssi = rssi;
        received.ackRequested = ackRequested;
        received.receivedAtMs = receivedAtMs;
        if (decodeStatus == radiosensors::protocol::DecodeStatus::Ok) {
            for (uint8_t index = 0; index < dataLength; ++index) {
                received.data[index] = rfm69.DATA[index];
            }
        }

        portENTER_CRITICAL(&statsMux);
        ++taskStats.packets;
        taskStats.bytes += dataLength;
        taskStats.lastPacketMs = receivedAtMs;
        taskStats.lastSenderId = senderId;
        taskStats.lastRssi = rssi;
        if (ackRequested) {
            ++taskStats.ackRequestsIgnored;
        }
        switch (decodeStatus) {
            case radiosensors::protocol::DecodeStatus::Ok:
                ++taskStats.v2Frames;
                if (frame.kind == radiosensors::protocol::FrameKind::Telemetry) {
                    ++taskStats.v2TelemetryFrames;
                }
                break;
            case radiosensors::protocol::DecodeStatus::EmptyFrame:
                ++taskStats.emptyApplicationFrames;
                break;
            case radiosensors::protocol::DecodeStatus::UnsupportedVersion:
                ++taskStats.unsupportedProtocolVersions;
                break;
            case radiosensors::protocol::DecodeStatus::UnsupportedKind:
                ++taskStats.unsupportedFrameKinds;
                break;
        }
        portEXIT_CRITICAL(&statsMux);

        if (decodeStatus == radiosensors::protocol::DecodeStatus::Ok) {
            const bool queued =
                xQueueSendToBack(receivedFrameQueue, &received, 0) == pdPASS;
            portENTER_CRITICAL(&statsMux);
            if (queued) ++taskStats.rxFramesQueued;
            else ++taskStats.rxFramesDropped;
            portEXIT_CRITICAL(&statsMux);
        }

        // receiveDone() leaves a completed packet in standby. Calling it again
        // after consuming the static 66-byte buffer returns the radio to RX.
        rfm69.receiveDone();
}

void processCommand(const RadioCommand& command) {
    if (command.kind == CommandKind::SetProfile) {
        if (command.profile == Profile::Commissioning) {
            if (config::commissioningEnabled) {
                rfm69.setNetwork(config::commissioningNetworkId);
                rfm69.encrypt(config::commissioningKey);
                currentProfile.store(Profile::Commissioning);
                currentNetworkId.store(config::commissioningNetworkId);
            }
        } else {
            rfm69.setNetwork(config::networkId);
            rfm69.encrypt(config::encryptionKey);
            currentProfile.store(Profile::Operational);
            currentNetworkId.store(config::networkId);
        }
    } else {
        rfm69.send(
            command.targetId,
            command.data,
            command.size,
            command.requestAck);
        if (command.switchAfterSend) {
            if (command.profile == Profile::Commissioning && config::commissioningEnabled) {
                rfm69.setNetwork(config::commissioningNetworkId);
                rfm69.encrypt(config::commissioningKey);
                currentProfile.store(Profile::Commissioning);
                currentNetworkId.store(config::commissioningNetworkId);
            } else if (command.profile == Profile::Operational) {
                rfm69.setNetwork(config::networkId);
                rfm69.encrypt(config::encryptionKey);
                currentProfile.store(Profile::Operational);
                currentNetworkId.store(config::networkId);
            }
        }
    }
    rfm69.receiveDone();
    portENTER_CRITICAL(&statsMux);
    ++taskStats.commandsProcessed;
    portEXIT_CRITICAL(&statsMux);
}

void radioTask(void*) {
    for (;;) {
        QueueSetMemberHandle_t ready = xQueueSelectFromSet(radioQueueSet, portMAX_DELAY);
        uint8_t signal = 0;
        if (ready == interruptQueue && xQueueReceive(interruptQueue, &signal, 0) == pdPASS) {
            drainReceivedFrame();
            continue;
        }

        // If RX and a command became ready together, always drain the FIFO first.
        if (xQueueReceive(interruptQueue, &signal, 0) == pdPASS) {
            drainReceivedFrame();
        }
        RadioCommand command{};
        if (xQueueReceive(commandQueue, &command, 0) == pdPASS) {
            processCommand(command);
        }
    }
}

}  // namespace

bool begin() {
    currentState.store(State::Starting);
    pinMode(config::interrupt, INPUT);

    if (!radioSpi.begin(config::sck, config::miso, config::mosi, config::chipSelect)) {
        currentState.store(State::SpiInitializationFailed);
        Serial.println("RFM69 SPI initialization failed; gateway will continue without radio");
        return false;
    }

    if (!rfm69.initialize(config::frequencyBand, config::nodeId, config::networkId)) {
        currentState.store(State::InitializationFailed);
        Serial.println("RFM69 initialization failed; gateway will continue without radio");
        return false;
    }

    detectedVersion = rfm69.getVersion();
    if (detectedVersion != kExpectedVersion) {
        currentState.store(State::VersionMismatch);
        rfm69.sleep();
        Serial.printf(
            "RFM69 version mismatch: expected 0x%02x, read 0x%02x\n",
            kExpectedVersion,
            detectedVersion);
        return false;
    }

    rfm69.setHighPower(config::highPower);
    rfm69.setPowerDBm(config::txPowerDbm);
    configuredFrequencyHz = rfm69.getFrequency();
    configuredBitRate = rfm69.getBitRate();

    if (!config::encryptionEnabled) {
        currentState.store(State::EncryptionKeyMissing);
        rfm69.sleep();
        Serial.println(
            "RFM69 encryption key is missing; radio reception is disabled");
        return false;
    }
    rfm69.encrypt(config::encryptionKey);

    interruptQueue = xQueueCreate(1, sizeof(uint8_t));
    receivedFrameQueue = xQueueCreate(kRxQueueDepth, sizeof(ReceivedFrame));
    commandQueue = xQueueCreate(kCommandQueueDepth, sizeof(RadioCommand));
    radioQueueSet = xQueueCreateSet(1 + kCommandQueueDepth);
    if (interruptQueue == nullptr || receivedFrameQueue == nullptr ||
        commandQueue == nullptr || radioQueueSet == nullptr ||
        xQueueAddToSet(interruptQueue, radioQueueSet) != pdPASS ||
        xQueueAddToSet(commandQueue, radioQueueSet) != pdPASS) {
        currentState.store(State::TaskFailed);
        rfm69.sleep();
        Serial.println("RFM69 queue creation failed");
        return false;
    }

    if (xTaskCreatePinnedToCore(
            radioTask,
            "rfm69-rx",
            kTaskStackSize,
            nullptr,
            kTaskPriority,
            &radioTaskHandle,
            kTaskCore) != pdPASS) {
        currentState.store(State::TaskFailed);
        rfm69.sleep();
        Serial.println("RFM69 receive task creation failed");
        return false;
    }

    rfm69.setIsrCallback(onRadioInterrupt);
    rfm69.receiveDone();
    currentState.store(State::Receiving);

    Serial.printf(
        "RFM69 ready: version=0x%02x frequency=%luHz bitrate=%lubps "
        "variant=%s power=%ddBm SPI=%s pins=%d/%d/%d/%d irq=%d\n",
        detectedVersion,
        configuredFrequencyHz,
        configuredBitRate,
        config::highPower ? "HW" : "W",
        config::txPowerDbm,
        spiHostName(),
        config::sck,
        config::miso,
        config::mosi,
        config::chipSelect,
        config::interrupt);
    return true;
}

bool receive(ReceivedFrame& frame, const TickType_t waitTicks) {
    return receivedFrameQueue != nullptr &&
           xQueueReceive(receivedFrameQueue, &frame, waitTicks) == pdPASS;
}

bool requestProfile(const Profile profile) {
    if (commandQueue == nullptr ||
        (profile == Profile::Commissioning && !config::commissioningEnabled)) {
        return false;
    }
    RadioCommand command{};
    command.kind = CommandKind::SetProfile;
    command.profile = profile;
    const bool queued = xQueueSendToBack(commandQueue, &command, 0) == pdPASS;
    portENTER_CRITICAL(&statsMux);
    if (queued) ++taskStats.commandsQueued;
    else ++taskStats.commandsDropped;
    portEXIT_CRITICAL(&statsMux);
    return queued;
}

bool send(
    const uint16_t targetId,
    const uint8_t* const data,
    const size_t size,
    const bool requestAck) {
    if (commandQueue == nullptr || data == nullptr || size > kMaxPayloadSize) {
        return false;
    }
    RadioCommand command{};
    command.kind = CommandKind::Send;
    command.targetId = targetId;
    command.size = static_cast<uint8_t>(size);
    command.requestAck = requestAck;
    for (size_t index = 0; index < size; ++index) command.data[index] = data[index];
    const bool queued = xQueueSendToBack(commandQueue, &command, 0) == pdPASS;
    portENTER_CRITICAL(&statsMux);
    if (queued) ++taskStats.commandsQueued;
    else ++taskStats.commandsDropped;
    portEXIT_CRITICAL(&statsMux);
    return queued;
}

bool sendThenSwitchProfile(
    const uint16_t targetId,
    const uint8_t* const data,
    const size_t size,
    const Profile profile) {
    if (commandQueue == nullptr || data == nullptr || size > kMaxPayloadSize ||
        (profile == Profile::Commissioning && !config::commissioningEnabled)) {
        return false;
    }
    RadioCommand command{};
    command.kind = CommandKind::Send;
    command.targetId = targetId;
    command.size = static_cast<uint8_t>(size);
    command.profile = profile;
    command.switchAfterSend = true;
    for (size_t index = 0; index < size; ++index) command.data[index] = data[index];
    const bool queued = xQueueSendToBack(commandQueue, &command, 0) == pdPASS;
    portENTER_CRITICAL(&statsMux);
    if (queued) ++taskStats.commandsQueued;
    else ++taskStats.commandsDropped;
    portEXIT_CRITICAL(&statsMux);
    return queued;
}

State state() {
    return currentState.load();
}

const char* stateName() {
    switch (state()) {
        case State::Stopped:
            return "stopped";
        case State::Starting:
            return "starting";
        case State::SpiInitializationFailed:
            return "spi_initialization_failed";
        case State::InitializationFailed:
            return "initialization_failed";
        case State::VersionMismatch:
            return "version_mismatch";
        case State::EncryptionKeyMissing:
            return "encryption_key_missing";
        case State::TaskFailed:
            return "task_failed";
        case State::Receiving:
            return "receiving";
    }
    return "unknown";
}

const char* frequencyBandName() {
    switch (config::frequencyBand) {
        case RF69_315MHZ:
            return "315";
        case RF69_433MHZ:
            return "433";
        case RF69_868MHZ:
            return "868";
        case RF69_915MHZ:
            return "915";
        default:
            return "unknown";
    }
}

const char* spiHostName() {
    if (config::spiHost == HSPI) {
        return "HSPI";
    }
    if (config::spiHost == FSPI) {
        return "FSPI";
    }
#if defined(VSPI)
    if (config::spiHost == VSPI) {
        return "VSPI";
    }
#endif
    return "unknown";
}

const char* profileName() {
    return currentProfile.load() == Profile::Commissioning
        ? "commissioning"
        : "operational";
}

Snapshot snapshot() {
    Snapshot result{
        state(),
        detectedVersion,
        configuredFrequencyHz,
        configuredBitRate,
        config::txPowerDbm,
        config::encryptionEnabled,
        0,
    };

    portENTER_CRITICAL(&statsMux);
    result.interrupts = taskStats.interrupts;
    result.packets = taskStats.packets;
    result.bytes = taskStats.bytes;
    result.emptyWakeups = taskStats.emptyWakeups;
    result.ackRequestsIgnored = taskStats.ackRequestsIgnored;
    result.v2Frames = taskStats.v2Frames;
    result.v2TelemetryFrames = taskStats.v2TelemetryFrames;
    result.emptyApplicationFrames = taskStats.emptyApplicationFrames;
    result.unsupportedProtocolVersions = taskStats.unsupportedProtocolVersions;
    result.unsupportedFrameKinds = taskStats.unsupportedFrameKinds;
    result.rxFramesQueued = taskStats.rxFramesQueued;
    result.rxFramesDropped = taskStats.rxFramesDropped;
    result.commandsQueued = taskStats.commandsQueued;
    result.commandsDropped = taskStats.commandsDropped;
    result.commandsProcessed = taskStats.commandsProcessed;
    result.profile = currentProfile.load();
    result.currentNetworkId = currentNetworkId.load();
    result.lastPacketMs = taskStats.lastPacketMs;
    result.lastSenderId = taskStats.lastSenderId;
    result.lastRssi = taskStats.lastRssi;
    portEXIT_CRITICAL(&statsMux);
    return result;
}

}  // namespace gateway::radio
