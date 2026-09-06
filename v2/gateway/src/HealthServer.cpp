#include "HealthServer.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESP.h>

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

namespace gateway::health {
namespace {

AsyncWebServer server(80);

void handleHealth(AsyncWebServerRequest* request) {
    const radio::Snapshot radioSnapshot = radio::snapshot();
    const commissioning::Snapshot commissioningSnapshot = commissioning::snapshot();
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

}  // namespace

void begin() {
    server.on("/health", HTTP_GET, handleHealth);
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "application/json", "{\"error\":\"not_found\"}");
    });
    server.begin();
    Serial.println("Health server listening on port 80");
}

}  // namespace gateway::health
