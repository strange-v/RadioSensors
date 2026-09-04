#include "HealthServer.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESP.h>

#include "BoardProfile.h"
#include "Diagnostics.h"
#include "DeviceIdentity.h"
#include "EthernetService.h"
#include "OtaService.h"

namespace gateway::health {
namespace {

AsyncWebServer server(80);

void handleHealth(AsyncWebServerRequest* request) {
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"status\":\"ok\",\"firmware\":\"%s\",\"board\":\"%s\",\"hostname\":\"%s\","
        "\"reset_reason\":\"%s\",\"uptime_ms\":%lu,\"free_heap\":%lu,"
        "\"ethernet\":{\"state\":\"%s\",\"has_ip\":%s,\"ip\":\"%s\","
        "\"mac\":\"%s\"},\"ota\":{\"enabled\":%s,\"state\":\"%s\",\"progress\":%u}}",
        GATEWAY_FIRMWARE_VERSION,
        board::current.name,
        identity::hostname(),
        diagnostics::resetReason(),
        millis(),
        ESP.getFreeHeap(),
        ethernet::stateName(),
        ethernet::hasIp() ? "true" : "false",
        ethernet::ipAddress().c_str(),
        ethernet::macAddress().c_str(),
        ota::enabled() ? "true" : "false",
        ota::stateName(),
        ota::progressPercent());
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
