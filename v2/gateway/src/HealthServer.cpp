#include "HealthServer.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESP.h>
#include <GatewayStream.h>

#include <atomic>

#include "BoardProfile.h"
#include "CommissioningService.h"
#include "Diagnostics.h"
#include "DeviceIdentity.h"
#include "EthernetService.h"
#include "FirmwareVersion.h"
#include "GatewayStatus.h"
#include "NodeRegistryStore.h"
#include "OtaService.h"
#include "RadioConfig.h"
#include "RadioService.h"
#include "TelemetryStore.h"
#include "TimeService.h"

namespace gateway::health {
namespace {

AsyncWebServer server(80);
AsyncWebSocket telemetrySocket("/ws");

constexpr uint16_t kWebSocketKeepAliveSeconds = 30;
constexpr uint16_t kMaximumWebSocketClients = 4;

std::atomic<uint32_t> websocketConnections{0};
std::atomic<uint32_t> websocketMessagesSent{0};
std::atomic<uint32_t> websocketMessagesDropped{0};

size_t encodeTelemetryFrame(
    const telemetry_store::Record& record,
    uint8_t* const output,
    const size_t capacity) {
    return radiosensors::stream::encodeTelemetry(
        record.sequence,
        record.nodeId,
        record.profileId,
        record.receivedAtUnixMs,
        record.rssi,
        record.data,
        record.size,
        output,
        capacity);
}

bool sendControl(
    AsyncWebSocketClient* const client,
    const radiosensors::stream::MessageKind kind,
    const uint32_t sequence) {
    uint8_t message[radiosensors::stream::kControlFrameSize]{};
    radiosensors::stream::encodeControl(kind, sequence, message, sizeof(message));
    return client->binary(message, sizeof(message));
}

bool sendTelemetry(
    AsyncWebSocketClient* const client,
    const telemetry_store::Record& record) {
    uint8_t message[radiosensors::stream::kTelemetryEnvelopeSize + radio::kMaxPayloadSize]{};
    const size_t size = encodeTelemetryFrame(record, message, sizeof(message));
    return client->binary(message, size);
}

void sendSnapshot(AsyncWebSocketClient* const client) {
    const telemetry_store::Snapshot initial = telemetry_store::snapshot();
    if (!sendControl(
            client, radiosensors::stream::MessageKind::SnapshotBegin, initial.updates)) return;

    for (uint8_t nodeId = radiosensors::registry::kFirstNodeId;
         nodeId <= radiosensors::registry::kLastNodeId;
         ++nodeId) {
        telemetry_store::Record record{};
        if (telemetry_store::find(nodeId, record) &&
            !sendTelemetry(client, record)) {
            return;
        }
    }

    const telemetry_store::Snapshot final = telemetry_store::snapshot();
    sendControl(client, radiosensors::stream::MessageKind::SnapshotEnd, final.updates);
}

void handleWebSocketEvent(
    AsyncWebSocket*,
    AsyncWebSocketClient* client,
    const AwsEventType type,
    void*,
    uint8_t*,
    size_t) {
    if (type == WS_EVT_CONNECT) {
        ++websocketConnections;
        client->setCloseClientOnQueueFull(true);
        client->keepAlivePeriod(kWebSocketKeepAliveSeconds);
        sendSnapshot(client);
    }
}

void handleHealth(AsyncWebServerRequest* request) {
    const radio::Snapshot radioSnapshot = radio::snapshot();
    const commissioning::Snapshot commissioningSnapshot = commissioning::snapshot();
    const telemetry_store::Snapshot telemetrySnapshot = telemetry_store::snapshot();
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"status\":\"ok\",\"firmware\":\"%s\",\"board\":\"%s\",\"hostname\":\"%s\","
        "\"reset_reason\":\"%s\",\"uptime_ms\":%lu,\"free_heap\":%lu,"
        "\"registry\":{\"records\":%u,\"generation\":%lu},"
        "\"pairing\":{\"active\":%s,\"remaining_seconds\":%lu,\"indication\":\"%s\"},"
        "\"commissioning\":{\"join_requests\":%lu,\"join_accepts_queued\":%lu,"
        "\"join_confirms\":%lu,\"join_completes_queued\":%lu,"
        "\"nodes_activated\":%lu,\"rejected_frames\":%lu,"
        "\"storage_errors\":%lu,\"confirm_timeouts\":%lu},"
        "\"ethernet\":{\"state\":\"%s\",\"has_ip\":%s,\"ip\":\"%s\","
        "\"mac\":\"%s\"},\"ota\":{\"enabled\":%s,\"state\":\"%s\",\"progress\":%u},"
        "\"telemetry\":{\"nodes_seen\":%u,\"updates\":%lu,"
        "\"last_node_id\":%u,\"last_received_at_ms\":%llu},"
        "\"time\":{\"state\":\"%s\",\"unix_ms\":%llu,"
        "\"last_sync_ms\":%llu},"
        "\"websocket\":{\"clients\":%u,\"connections\":%lu,"
        "\"messages_sent\":%lu,\"messages_dropped\":%lu},"
        "\"radio\":{\"state\":\"%s\",\"present\":%s,\"version\":%u,"
        "\"frequency_band_mhz\":\"%s\",\"frequency_hz\":%lu,\"bit_rate\":%lu,"
        "\"node_id\":%u,\"network_id\":%u,\"variant\":\"%s\","
        "\"configured_power_dbm\":%d,\"encryption_enabled\":%s,"
        "\"profile\":\"%s\",\"spi_host\":\"%s\","
        "\"pins\":{\"sck\":%d,\"miso\":%d,\"mosi\":%d,\"cs\":%d,\"irq\":%d},"
        "\"counters\":{\"interrupts\":%lu,\"packets\":%lu,\"bytes\":%lu,"
        "\"empty_wakeups\":%lu,\"ack_requests_ignored\":%lu,"
        "\"telemetry_acks_sent\":%lu,\"telemetry_rejected_inactive\":%lu,"
        "\"telemetry_frames_queued\":%lu,\"telemetry_frames_dropped\":%lu,"
        "\"v2_frames\":%lu,\"v2_telemetry_frames\":%lu,"
        "\"empty_application_frames\":%lu,"
        "\"unsupported_protocol_versions\":%lu,"
        "\"unsupported_frame_kinds\":%lu,"
        "\"rx_frames_queued\":%lu,\"rx_frames_dropped\":%lu,"
        "\"commands_queued\":%lu,\"commands_dropped\":%lu,"
        "\"commands_processed\":%lu},"
        "\"last_packet\":{\"at_ms\":%lu,\"sender_id\":%u,\"rssi\":%d}}}",
        firmware::version,
        board::current.name,
        identity::hostname(),
        diagnostics::resetReason(),
        millis(),
        ESP.getFreeHeap(),
        static_cast<unsigned>(registry_store::recordCount()),
        static_cast<unsigned long>(registry_store::generation()),
        status::pairingActive() ? "true" : "false",
        static_cast<unsigned long>(status::pairingRemainingSeconds()),
        status::indicationName(),
        commissioningSnapshot.joinRequests,
        commissioningSnapshot.joinAcceptsQueued,
        commissioningSnapshot.joinConfirms,
        commissioningSnapshot.joinCompletesQueued,
        commissioningSnapshot.nodesActivated,
        commissioningSnapshot.rejectedFrames,
        commissioningSnapshot.storageErrors,
        commissioningSnapshot.confirmTimeouts,
        ethernet::stateName(),
        ethernet::hasIp() ? "true" : "false",
        ethernet::ipAddress().c_str(),
        ethernet::macAddress().c_str(),
        ota::enabled() ? "true" : "false",
        ota::stateName(),
        ota::progressPercent(),
        static_cast<unsigned>(telemetrySnapshot.nodesSeen),
        static_cast<unsigned long>(telemetrySnapshot.updates),
        telemetrySnapshot.hasLast ? telemetrySnapshot.last.nodeId : 0,
        static_cast<unsigned long long>(
            telemetrySnapshot.hasLast ? telemetrySnapshot.last.receivedAtUnixMs : 0),
        time_service::stateName(),
        static_cast<unsigned long long>(time_service::unixTimeMs()),
        static_cast<unsigned long long>(time_service::lastSyncUnixMs()),
        static_cast<unsigned>(telemetrySocket.count()),
        static_cast<unsigned long>(websocketConnections.load()),
        static_cast<unsigned long>(websocketMessagesSent.load()),
        static_cast<unsigned long>(websocketMessagesDropped.load()),
        radio::stateName(),
        radioSnapshot.version == 0x24 ? "true" : "false",
        radioSnapshot.version,
        radio::frequencyBandName(),
        radioSnapshot.frequencyHz,
        radioSnapshot.bitRate,
        radio::config::nodeId,
        radioSnapshot.currentNetworkId,
        radio::config::highPower ? "HW" : "W",
        radioSnapshot.configuredPowerDbm,
        radioSnapshot.encryptionEnabled ? "true" : "false",
        radio::profileName(),
        radio::spiHostName(),
        radio::config::sck,
        radio::config::miso,
        radio::config::mosi,
        radio::config::chipSelect,
        radio::config::interrupt,
        radioSnapshot.interrupts,
        radioSnapshot.packets,
        radioSnapshot.bytes,
        radioSnapshot.emptyWakeups,
        radioSnapshot.ackRequestsIgnored,
        radioSnapshot.telemetryAcksSent,
        radioSnapshot.telemetryRejectedInactive,
        radioSnapshot.telemetryFramesQueued,
        radioSnapshot.telemetryFramesDropped,
        radioSnapshot.v2Frames,
        radioSnapshot.v2TelemetryFrames,
        radioSnapshot.emptyApplicationFrames,
        radioSnapshot.unsupportedProtocolVersions,
        radioSnapshot.unsupportedFrameKinds,
        radioSnapshot.rxFramesQueued,
        radioSnapshot.rxFramesDropped,
        radioSnapshot.commandsQueued,
        radioSnapshot.commandsDropped,
        radioSnapshot.commandsProcessed,
        radioSnapshot.lastPacketMs,
        radioSnapshot.lastSenderId,
        radioSnapshot.lastRssi);
    request->send(response);
}

void handleLastTelemetry(AsyncWebServerRequest* request) {
    const telemetry_store::Snapshot telemetrySnapshot = telemetry_store::snapshot();
    if (!telemetrySnapshot.hasLast) {
        request->send(404, "application/json", "{\"error\":\"no_telemetry\"}");
        return;
    }

    const telemetry_store::Record& record = telemetrySnapshot.last;
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"node_id\":%u,\"profile_id\":%u,\"received_at_ms\":%llu,"
        "\"rssi\":%d,\"size\":%u,\"sequence\":%lu,\"payload_hex\":\"",
        record.nodeId,
        record.profileId,
        static_cast<unsigned long long>(record.receivedAtUnixMs),
        record.rssi,
        record.size,
        static_cast<unsigned long>(record.sequence));
    for (uint8_t index = 0; index < record.size; ++index) {
        response->printf("%02x", record.data[index]);
    }
    response->print("\"}");
    request->send(response);
}

}  // namespace

void begin() {
    telemetrySocket.onEvent(handleWebSocketEvent);
    server.addHandler(&telemetrySocket);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/telemetry/last", HTTP_GET, handleLastTelemetry);
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "application/json", "{\"error\":\"not_found\"}");
    });
    server.begin();
    Serial.println("Health server listening on port 80");
}

void loop() {
    telemetrySocket.cleanupClients(kMaximumWebSocketClients);
}

void publishTelemetry(const telemetry_store::Record& record) {
    if (telemetrySocket.count() == 0) return;
    uint8_t message[radiosensors::stream::kTelemetryEnvelopeSize + radio::kMaxPayloadSize]{};
    const size_t size = encodeTelemetryFrame(record, message, sizeof(message));
    const AsyncWebSocket::SendStatus status = telemetrySocket.binaryAll(message, size);
    if (status == AsyncWebSocket::DISCARDED) {
        ++websocketMessagesDropped;
    } else {
        ++websocketMessagesSent;
        if (status == AsyncWebSocket::PARTIALLY_ENQUEUED) {
            ++websocketMessagesDropped;
        }
    }
}

}  // namespace gateway::health
