#include "HealthServer.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ESP.h>
#include <GatewayStream.h>
#include <JoinRequest.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <atomic>

#include "ApiVersion.h"
#include "AuthenticationService.h"
#include "BoardProfile.h"
#include "CommissioningService.h"
#include "ConfigurationStore.h"
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
#include "WebUiService.h"

namespace gateway::health {
namespace {

AsyncWebServer server(80);
AsyncWebSocket telemetrySocket("/ws");

constexpr uint16_t kWebSocketKeepAliveSeconds = 30;
constexpr uint16_t kMaximumWebSocketClients = 4;

std::atomic<uint32_t> websocketConnections{0};
std::atomic<uint32_t> websocketMessagesSent{0};
std::atomic<uint32_t> websocketMessagesDropped{0};
SemaphoreHandle_t setupMutex = nullptr;
SemaphoreHandle_t managementMutex = nullptr;
constexpr uint32_t kPasswordIterations = 100000;
constexpr const char* kSessionCookieName = "rs_session";

void sendError(AsyncWebServerRequest* request, int status, const char* code);

const char* roleName(const radiosensors::gateway_storage::UserRole role) {
    return role == radiosensors::gateway_storage::UserRole::Admin
        ? "admin" : "viewer";
}

bool readSessionToken(
    AsyncWebServerRequest* request,
    char output[authentication::kSessionTokenCharacters + 1]) {
    if (!request->hasHeader("Cookie")) return false;
    const String cookies = request->getHeader("Cookie")->value();
    const String prefix = String(kSessionCookieName) + "=";
    int position = 0;
    while ((position = cookies.indexOf(prefix, position)) >= 0) {
        if (position == 0 || cookies[position - 1] == ';' ||
            cookies[position - 1] == ' ') {
            const int valueStart = position + prefix.length();
            int valueEnd = cookies.indexOf(';', valueStart);
            if (valueEnd < 0) valueEnd = cookies.length();
            if (valueEnd - valueStart ==
                static_cast<int>(authentication::kSessionTokenCharacters)) {
                memcpy(
                    output, cookies.c_str() + valueStart,
                    authentication::kSessionTokenCharacters);
                output[authentication::kSessionTokenCharacters] = '\0';
                return true;
            }
        }
        position += prefix.length();
    }
    return false;
}

void addSessionCookie(
    AsyncWebServerResponse* response, const char* sessionToken) {
    char value[160]{};
    snprintf(
        value, sizeof(value),
        "%s=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200",
        kSessionCookieName, sessionToken);
    response->addHeader("Set-Cookie", value);
}

void clearSessionCookie(AsyncWebServerResponse* response) {
    response->addHeader(
        "Set-Cookie",
        "rs_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

void sendPrincipal(
    AsyncWebServerRequest* request,
    const authentication::Principal& principal,
    const char* sessionToken = nullptr,
    const int statusCode = 200) {
    char body[256]{};
    snprintf(
        body, sizeof(body),
        "{\"user\":{\"id\":%lu,\"username\":\"%s\",\"role\":\"%s\"},"
        "\"csrf_token\":\"%s\"}",
        static_cast<unsigned long>(principal.userId), principal.username,
        roleName(principal.role), principal.csrfToken);
    AsyncWebServerResponse* response =
        request->beginResponse(statusCode, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    if (sessionToken != nullptr) addSessionCookie(response, sessionToken);
    request->send(response);
}

bool authorizeSession(
    AsyncWebServerRequest* request, authentication::Principal& principal,
    const bool requireCsrf = false) {
    char sessionToken[authentication::kSessionTokenCharacters + 1]{};
    if (!readSessionToken(request, sessionToken) ||
        !authentication::authorize(
            sessionToken, nullptr, false, principal)) {
        sendError(request, 401, "authentication_required");
        return false;
    }
    if (!requireCsrf) return true;
    String csrfHeader;
    const char* csrfToken = nullptr;
    if (request->hasHeader("X-CSRF-Token")) {
        csrfHeader = request->getHeader("X-CSRF-Token")->value();
        csrfToken = csrfHeader.c_str();
    }
    if (!authentication::authorize(
            sessionToken, csrfToken, true, principal)) {
        sendError(request, 403, "invalid_csrf_token");
        return false;
    }
    return true;
}

bool authorizeAdmin(
    AsyncWebServerRequest* request, authentication::Principal& principal,
    const bool requireCsrf) {
    if (!authorizeSession(request, principal, requireCsrf)) return false;
    if (principal.role != radiosensors::gateway_storage::UserRole::Admin) {
        sendError(request, 403, "admin_required");
        return false;
    }
    return true;
}

bool authorizeRegistryRead(AsyncWebServerRequest* request) {
    char sessionToken[authentication::kSessionTokenCharacters + 1]{};
    authentication::Principal principal{};
    if (readSessionToken(request, sessionToken) &&
        authentication::authorize(sessionToken, nullptr, false, principal)) {
        return true;
    }
    if (request->hasHeader("Authorization")) {
        const String header = request->getHeader("Authorization")->value();
        if (header.startsWith("Bearer ") &&
            authentication::authorizeBearer(
                header.c_str() + 7,
                radiosensors::gateway_storage::TokenScope::RegistryRead)) {
            return true;
        }
    }
    sendError(request, 401, "authentication_required");
    return false;
}

bool authorizeTelemetryStream(AsyncWebServerRequest* request) {
    char sessionToken[authentication::kSessionTokenCharacters + 1]{};
    authentication::Principal principal{};
    if (readSessionToken(request, sessionToken) &&
        authentication::authorize(sessionToken, nullptr, false, principal)) {
        return true;
    }
    if (!request->hasHeader("Authorization")) return false;
    const String header = request->getHeader("Authorization")->value();
    return header.startsWith("Bearer ") &&
        authentication::authorizeBearer(
            header.c_str() + 7,
            radiosensors::gateway_storage::TokenScope::TelemetryRead);
}

void sendError(AsyncWebServerRequest* request, int status, const char* code) {
    char body[96]{};
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", code);
    request->send(status, "application/json", body);
}

void handleSetupStatus(AsyncWebServerRequest* request) {
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"setup_required\":%s,\"physical_window_active\":%s,"
        "\"remaining_seconds\":%lu}",
        status::setupRequired() ? "true" : "false",
        status::setupActive() ? "true" : "false",
        static_cast<unsigned long>(status::setupRemainingSeconds()));
    request->send(response);
}

uint8_t randomOperationalNetworkId(uint8_t commissioningNetworkId) {
    uint8_t value = 0;
    do value = static_cast<uint8_t>(esp_random());
    while (value == 0 || value == commissioningNetworkId);
    return value;
}

void handleInitialSetup(AsyncWebServerRequest* request, JsonVariant& json) {
    if (setupMutex == nullptr || xSemaphoreTake(setupMutex, 0) != pdTRUE) {
        sendError(request, 409, "setup_busy");
        return;
    }
    if (!status::setupRequired()) {
        xSemaphoreGive(setupMutex);
        sendError(request, 409, "setup_already_complete");
        return;
    }
    if (!status::setupActive()) {
        xSemaphoreGive(setupMutex);
        sendError(request, 403, "physical_setup_required");
        return;
    }
    if (!json.is<JsonObject>()) {
        xSemaphoreGive(setupMutex);
        sendError(request, 400, "invalid_request");
        return;
    }
    JsonObject object = json.as<JsonObject>();
    const char* username = object["username"].is<const char*>()
        ? object["username"].as<const char*>() : nullptr;
    const char* password = object["password"].is<const char*>()
        ? object["password"].as<const char*>() : nullptr;
    const char* displayName = object["display_name"].is<const char*>()
        ? object["display_name"].as<const char*>() : "";
    const size_t usernameLength = username == nullptr ? 0 : strlen(username);
    const size_t passwordLength = password == nullptr ? 0 : strlen(password);
    const size_t displayNameLength = strlen(displayName);
    if (usernameLength == 0 || usernameLength > radiosensors::gateway_storage::kUsernameSize ||
        passwordLength < 8 || passwordLength > 128 ||
        displayNameLength > radiosensors::gateway_storage::kDisplayNameSize) {
        xSemaphoreGive(setupMutex);
        sendError(request, 422, "invalid_setup_values");
        return;
    }

    auto secrets = configuration_store::secrets();
    if (!secrets.installationKeyPresent) {
        secrets.installationKeyPresent = true;
        esp_fill_random(secrets.installationKey,
                        radiosensors::gateway_storage::kRadioKeySize);
        secrets.operationalNetworkId = randomOperationalNetworkId(
            secrets.commissioningNetworkId);
    }
    if (object["operational_network_id"].is<uint8_t>()) {
        const uint8_t requested = object["operational_network_id"].as<uint8_t>();
        if (requested == 0 ||
            (secrets.commissioningKeyPresent &&
             requested == secrets.commissioningNetworkId)) {
            xSemaphoreGive(setupMutex);
            sendError(request, 422, "invalid_operational_network_id");
            return;
        }
        secrets.operationalNetworkId = requested;
    }

    auto settings = configuration_store::settings();
    memset(settings.displayName, 0, sizeof(settings.displayName));
    settings.displayNameLength = static_cast<uint8_t>(displayNameLength);
    memcpy(settings.displayName, displayName, displayNameLength);

    auto authentication = radiosensors::gateway_storage::defaultAuthentication();
    authentication.userCount = 1;
    authentication.nextUserId = 2;
    auto& user = authentication.users[0];
    user.id = 1;
    user.usernameLength = static_cast<uint8_t>(usernameLength);
    memcpy(user.username, username, usernameLength);
    user.role = radiosensors::gateway_storage::UserRole::Admin;
    user.enabled = true;
    user.hashAlgorithm =
        radiosensors::gateway_storage::PasswordHashAlgorithm::Pbkdf2HmacSha256;
    user.pbkdf2Iterations = kPasswordIterations;
    esp_fill_random(user.salt, sizeof(user.salt));
    const uint32_t hashStartedAt = millis();
    const int hashResult = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        reinterpret_cast<const unsigned char*>(password), passwordLength,
        user.salt, sizeof(user.salt), user.pbkdf2Iterations,
        sizeof(user.passwordHash), user.passwordHash);
    if (hashResult != 0) {
        xSemaphoreGive(setupMutex);
        sendError(request, 500, "password_hash_failed");
        return;
    }
    Serial.printf("Initial password hash completed in %lu ms (%lu iterations)\n",
                  static_cast<unsigned long>(millis() - hashStartedAt),
                  static_cast<unsigned long>(user.pbkdf2Iterations));

    const auto secretResult = configuration_store::saveSecrets(secrets);
    if ((secretResult != configuration_store::SaveStatus::Ok &&
         secretResult != configuration_store::SaveStatus::NoChange)) {
        xSemaphoreGive(setupMutex);
        sendError(request, 500, "setup_storage_failed");
        return;
    }
    const auto settingsResult = configuration_store::saveSettings(settings);
    if (settingsResult != configuration_store::SaveStatus::Ok &&
        settingsResult != configuration_store::SaveStatus::NoChange) {
        xSemaphoreGive(setupMutex);
        sendError(request, 500, "setup_storage_failed");
        return;
    }
    if (configuration_store::saveAuthentication(authentication) !=
        configuration_store::SaveStatus::Ok) {
        xSemaphoreGive(setupMutex);
        sendError(request, 500, "setup_storage_failed");
        return;
    }

    status::closeSetup();
    xSemaphoreGive(setupMutex);
    authentication::LoginResult login{};
    if (authentication::createSession(
            user.id, user.role, user.username, user.usernameLength, login) ==
        authentication::LoginStatus::Ok) {
        sendPrincipal(request, login.principal, login.sessionToken, 201);
    } else {
        request->send(201, "application/json", "{\"status\":\"configured\"}");
    }
}

void handleLogin(AsyncWebServerRequest* request, JsonVariant& json) {
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    const JsonObject object = json.as<JsonObject>();
    const char* username = object["username"].is<const char*>()
        ? object["username"].as<const char*>() : nullptr;
    const char* password = object["password"].is<const char*>()
        ? object["password"].as<const char*>() : nullptr;
    authentication::LoginResult result{};
    switch (authentication::login(username, password, result)) {
        case authentication::LoginStatus::Ok:
            sendPrincipal(request, result.principal, result.sessionToken);
            return;
        case authentication::LoginStatus::InvalidCredentials:
            sendError(request, 401, "invalid_credentials");
            return;
        case authentication::LoginStatus::Busy:
            sendError(request, 409, "login_busy");
            return;
        case authentication::LoginStatus::StorageUnavailable:
            sendError(request, 503, "storage_unavailable");
            return;
        case authentication::LoginStatus::NoSessionCapacity:
            sendError(request, 503, "session_unavailable");
            return;
    }
}

void handleCurrentSession(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeSession(request, principal)) return;
    sendPrincipal(request, principal);
}

void handleLogout(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeSession(request, principal, true)) return;
    char sessionToken[authentication::kSessionTokenCharacters + 1]{};
    readSessionToken(request, sessionToken);
    authentication::logout(sessionToken);
    AsyncWebServerResponse* response = request->beginResponse(204);
    clearSessionCookie(response);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

const char* nodeStateName(const radiosensors::registry::NodeState state) {
    switch (state) {
        case radiosensors::registry::NodeState::Pending: return "pending";
        case radiosensors::registry::NodeState::Active: return "active";
        case radiosensors::registry::NodeState::Disabled: return "disabled";
    }
    return "unknown";
}

void handleNodes(AsyncWebServerRequest* request) {
    if (!authorizeRegistryRead(request)) return;
    registry_store::Snapshot registry{};
    if (!registry_store::snapshot(registry)) {
        sendError(request, 503, "registry_unavailable");
        return;
    }
    AsyncResponseStream* response =
        request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"registry_generation\":%lu,\"nodes\":[",
        static_cast<unsigned long>(registry.generation));
    for (size_t index = 0; index < registry.count; ++index) {
        const auto& node = registry.records[index];
        if (index != 0) response->print(',');
        response->printf(
            "{\"node_id\":%u,\"profile_id\":%u,"
            "\"firmware\":\"%u.%u.%u\",\"state\":\"%s\"",
            node.nodeId, node.profileId, node.firmware.major,
            node.firmware.minor, node.firmware.patch,
            nodeStateName(node.state));
        telemetry_store::Record telemetry{};
        if (telemetry_store::find(node.nodeId, telemetry)) {
            response->printf(
                ",\"last_seen_at_ms\":%llu,\"rssi\":%d,"
                "\"has_telemetry\":true",
                static_cast<unsigned long long>(telemetry.receivedAtUnixMs),
                telemetry.rssi);
        } else {
            response->print(",\"has_telemetry\":false");
        }
        response->print('}');
    }
    response->print("]}");
    request->send(response);
}

bool parseTokenScopes(const JsonVariantConst& json, uint16_t& scopes) {
    scopes = 0;
    if (!json.is<JsonArrayConst>()) return false;
    for (const JsonVariantConst item : json.as<JsonArrayConst>()) {
        if (!item.is<const char*>()) return false;
        const char* value = item.as<const char*>();
        if (strcmp(value, "gateway:read") == 0) {
            scopes |= radiosensors::gateway_storage::TokenScope::GatewayRead;
        } else if (strcmp(value, "registry:read") == 0) {
            scopes |= radiosensors::gateway_storage::TokenScope::RegistryRead;
        } else if (strcmp(value, "telemetry:read") == 0) {
            scopes |= radiosensors::gateway_storage::TokenScope::TelemetryRead;
        } else {
            return false;
        }
    }
    return scopes != 0;
}

void addTokenScopes(JsonArray output, const uint16_t scopes) {
    if ((scopes & radiosensors::gateway_storage::TokenScope::GatewayRead) != 0)
        output.add("gateway:read");
    if ((scopes & radiosensors::gateway_storage::TokenScope::RegistryRead) != 0)
        output.add("registry:read");
    if ((scopes & radiosensors::gateway_storage::TokenScope::TelemetryRead) != 0)
        output.add("telemetry:read");
}

void handleTokens(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, false)) return;
    const auto authenticationData = configuration_store::authentication();
    JsonDocument document;
    JsonArray tokens = document["tokens"].to<JsonArray>();
    for (uint8_t index = 0; index < authenticationData.tokenCount; ++index) {
        const auto& source = authenticationData.tokens[index];
        JsonObject token = tokens.add<JsonObject>();
        token["id"] = source.id;
        token["name"] = String(source.name, source.nameLength);
        token["enabled"] = source.enabled;
        token["created_at_ms"] = source.createdAtUnixMs;
        addTokenScopes(token["scopes"].to<JsonArray>(), source.scopes);
    }
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
}

void handleCreateToken(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>() || managementMutex == nullptr ||
        xSemaphoreTake(managementMutex, 0) != pdTRUE) {
        sendError(request, json.is<JsonObject>() ? 409 : 400,
                  json.is<JsonObject>() ? "mutation_busy" : "invalid_request");
        return;
    }
    const JsonObject object = json.as<JsonObject>();
    const char* name = object["name"].is<const char*>()
        ? object["name"].as<const char*>() : nullptr;
    const size_t nameLength = name == nullptr ? 0 : strlen(name);
    uint16_t scopes = 0;
    if (nameLength == 0 ||
        nameLength > radiosensors::gateway_storage::kCredentialNameSize ||
        !parseTokenScopes(object["scopes"], scopes)) {
        xSemaphoreGive(managementMutex);
        sendError(request, 422, "invalid_token_values");
        return;
    }
    auto authenticationData = configuration_store::authentication();
    if (authenticationData.tokenCount >=
        radiosensors::gateway_storage::kMaximumTokens ||
        authenticationData.nextTokenId == UINT32_MAX) {
        xSemaphoreGive(managementMutex);
        sendError(request, 409, "token_capacity_reached");
        return;
    }
    char rawToken[authentication::kApiTokenCharacters + 1]{};
    auto& token = authenticationData.tokens[authenticationData.tokenCount];
    if (!authentication::createApiToken(rawToken, token.tokenHash)) {
        xSemaphoreGive(managementMutex);
        sendError(request, 500, "token_generation_failed");
        return;
    }
    token.id = authenticationData.nextTokenId == 0
        ? 1 : authenticationData.nextTokenId;
    authenticationData.nextTokenId = token.id + 1U;
    token.nameLength = static_cast<uint8_t>(nameLength);
    memcpy(token.name, name, nameLength);
    token.enabled = true;
    token.scopes = scopes;
    token.createdAtUnixMs = time_service::unixTimeMs();
    ++authenticationData.tokenCount;
    const auto saved = configuration_store::saveAuthentication(authenticationData);
    xSemaphoreGive(managementMutex);
    if (saved != configuration_store::SaveStatus::Ok) {
        memset(rawToken, 0, sizeof(rawToken));
        sendError(request, 500, "token_storage_failed");
        return;
    }

    JsonDocument document;
    document["id"] = token.id;
    document["name"] = String(token.name, token.nameLength);
    document["token"] = rawToken;
    document["created_at_ms"] = token.createdAtUnixMs;
    addTokenScopes(document["scopes"].to<JsonArray>(), token.scopes);
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->setCode(201);
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
    memset(rawToken, 0, sizeof(rawToken));
}

void handleDeleteToken(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>() || !json["id"].is<uint32_t>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    if (managementMutex == nullptr ||
        xSemaphoreTake(managementMutex, 0) != pdTRUE) {
        sendError(request, 409, "mutation_busy");
        return;
    }
    auto authenticationData = configuration_store::authentication();
    const uint32_t id = json["id"].as<uint32_t>();
    uint8_t index = 0;
    while (index < authenticationData.tokenCount &&
           authenticationData.tokens[index].id != id) ++index;
    if (index == authenticationData.tokenCount) {
        xSemaphoreGive(managementMutex);
        sendError(request, 404, "token_not_found");
        return;
    }
    for (uint8_t move = index + 1; move < authenticationData.tokenCount; ++move) {
        authenticationData.tokens[move - 1] = authenticationData.tokens[move];
    }
    --authenticationData.tokenCount;
    memset(
        &authenticationData.tokens[authenticationData.tokenCount], 0,
        sizeof(authenticationData.tokens[0]));
    const auto saved = configuration_store::saveAuthentication(authenticationData);
    xSemaphoreGive(managementMutex);
    if (saved != configuration_store::SaveStatus::Ok) {
        sendError(request, 500, "token_storage_failed");
        return;
    }
    request->send(204);
}

void sendSettings(AsyncWebServerRequest* request) {
    const auto settings = configuration_store::settings();
    JsonDocument document;
    document["generation"] = configuration_store::settingsGeneration();
    document["display_name"] =
        String(settings.displayName, settings.displayNameLength);
    document["mdns_enabled"] = settings.mdnsEnabled;
    document["ntp_enabled"] = settings.ntpEnabled;
    document["pairing_window_seconds"] = settings.pairingWindowSeconds;
    document["setup_window_seconds"] = settings.setupWindowSeconds;
    JsonArray servers = document["ntp_servers"].to<JsonArray>();
    for (uint8_t index = 0; index < settings.ntpServerCount; ++index) {
        servers.add(String(
            settings.ntpServers[index], settings.ntpServerLengths[index]));
    }
    AsyncResponseStream* response =
        request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
}

void handleSettings(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeSession(request, principal)) return;
    sendSettings(request);
}

void handleUpdateSettings(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    if (managementMutex == nullptr ||
        xSemaphoreTake(managementMutex, 0) != pdTRUE) {
        sendError(request, 409, "mutation_busy");
        return;
    }
    const JsonObject object = json.as<JsonObject>();
    const char* displayName = object["display_name"].is<const char*>()
        ? object["display_name"].as<const char*>() : nullptr;
    const size_t displayNameLength =
        displayName == nullptr ? 0 : strlen(displayName);
    const bool shapeValid =
        object["mdns_enabled"].is<bool>() &&
        object["ntp_enabled"].is<bool>() &&
        object["pairing_window_seconds"].is<uint16_t>() &&
        object["setup_window_seconds"].is<uint16_t>() &&
        object["ntp_servers"].is<JsonArrayConst>();
    const uint16_t pairingSeconds =
        object["pairing_window_seconds"] | static_cast<uint16_t>(0);
    const uint16_t setupSeconds =
        object["setup_window_seconds"] | static_cast<uint16_t>(0);
    const JsonArrayConst servers = object["ntp_servers"].as<JsonArrayConst>();
    if (!shapeValid ||
        displayNameLength > radiosensors::gateway_storage::kDisplayNameSize ||
        pairingSeconds < 30 || pairingSeconds > 900 ||
        setupSeconds < 60 || setupSeconds > 1800 ||
        servers.size() > radiosensors::gateway_storage::kNtpServerCount) {
        xSemaphoreGive(managementMutex);
        sendError(request, 422, "invalid_settings_values");
        return;
    }

    auto settings = configuration_store::settings();
    memset(settings.displayName, 0, sizeof(settings.displayName));
    settings.displayNameLength = static_cast<uint8_t>(displayNameLength);
    memcpy(settings.displayName, displayName, displayNameLength);
    settings.mdnsEnabled = object["mdns_enabled"].as<bool>();
    settings.ntpEnabled = object["ntp_enabled"].as<bool>();
    settings.pairingWindowSeconds = pairingSeconds;
    settings.setupWindowSeconds = setupSeconds;
    settings.ntpServerCount = 0;
    memset(settings.ntpServerLengths, 0, sizeof(settings.ntpServerLengths));
    memset(settings.ntpServers, 0, sizeof(settings.ntpServers));
    for (const JsonVariantConst item : servers) {
        if (!item.is<const char*>()) {
            xSemaphoreGive(managementMutex);
            sendError(request, 422, "invalid_settings_values");
            return;
        }
        const char* server = item.as<const char*>();
        const size_t length = strlen(server);
        if (length == 0 ||
            length > radiosensors::gateway_storage::kNtpServerSize) {
            xSemaphoreGive(managementMutex);
            sendError(request, 422, "invalid_settings_values");
            return;
        }
        const uint8_t index = settings.ntpServerCount++;
        settings.ntpServerLengths[index] = static_cast<uint8_t>(length);
        memcpy(settings.ntpServers[index], server, length);
    }
    const auto saved = configuration_store::saveSettings(settings);
    xSemaphoreGive(managementMutex);
    if (saved == configuration_store::SaveStatus::Invalid) {
        sendError(request, 422, "invalid_settings_values");
        return;
    }
    if (saved != configuration_store::SaveStatus::Ok &&
        saved != configuration_store::SaveStatus::NoChange) {
        sendError(request, 500, "settings_storage_failed");
        return;
    }
    sendSettings(request);
}

void sendPairingStatus(AsyncWebServerRequest* request) {
    char body[128]{};
    snprintf(
        body, sizeof(body),
        "{\"active\":%s,\"remaining_seconds\":%lu,\"indication\":\"%s\"}",
        status::pairingActive() ? "true" : "false",
        static_cast<unsigned long>(status::pairingRemainingSeconds()),
        status::indicationName());
    request->send(200, "application/json", body);
}

int hexNibble(const char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decodeHex(const char* input, uint8_t* output, const size_t size) {
    if (input == nullptr || strlen(input) != size * 2) return false;
    for (size_t index = 0; index < size; ++index) {
        const int high = hexNibble(input[index * 2]);
        const int low = hexNibble(input[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

void handleOpenPairing(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    const JsonObject object = json.as<JsonObject>();
    const char* uidHex = object["device_uid"].is<const char*>()
        ? object["device_uid"].as<const char*>() : nullptr;
    const char* keyHex = object["factory_key"].is<const char*>()
        ? object["factory_key"].as<const char*>() : nullptr;
    uint8_t deviceUid[radiosensors::protocol::kDeviceUidSize]{};
    uint8_t factoryKey[radiosensors::gateway_storage::kRadioKeySize]{};
    if (!decodeHex(uidHex, deviceUid, sizeof(deviceUid)) ||
        !decodeHex(keyHex, factoryKey, sizeof(factoryKey))) {
        memset(factoryKey, 0, sizeof(factoryKey));
        sendError(request, 422, "invalid_pairing_credentials");
        return;
    }
    const bool opened = commissioning::open(deviceUid, factoryKey);
    memset(factoryKey, 0, sizeof(factoryKey));
    if (!opened) {
        sendError(request, 503, "pairing_unavailable");
        return;
    }
    sendPairingStatus(request);
}

void handleClosePairing(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!commissioning::close()) {
        sendError(request, 503, "pairing_unavailable");
        return;
    }
    sendPairingStatus(request);
}

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
        "{\"status\":\"ok\",\"firmware\":\"%s\",\"api_version\":%u,\"board\":\"%s\",\"hostname\":\"%s\","
        "\"reset_reason\":\"%s\",\"uptime_ms\":%lu,\"free_heap\":%lu,"
        "\"registry\":{\"records\":%u,\"generation\":%lu},"
        "\"setup\":{\"required\":%s,\"active\":%s,\"remaining_seconds\":%lu},"
        "\"storage\":{\"ready\":%s,\"settings_generation\":%lu,"
        "\"auth_generation\":%lu,\"secrets_generation\":%lu},"
        "\"pairing\":{\"active\":%s,\"remaining_seconds\":%lu,\"indication\":\"%s\"},"
        "\"commissioning\":{\"join_requests\":%lu,\"join_accepts_queued\":%lu,"
        "\"join_confirms\":%lu,\"join_completes_queued\":%lu,"
        "\"nodes_activated\":%lu,\"rejected_frames\":%lu,"
        "\"storage_errors\":%lu,\"confirm_timeouts\":%lu},"
        "\"ethernet\":{\"state\":\"%s\",\"has_ip\":%s,\"ip\":\"%s\","
        "\"mac\":\"%s\"},\"ota\":{\"enabled\":%s,\"state\":\"%s\",\"progress\":%u},"
        "\"web_ui\":{\"state\":\"%s\",\"version\":\"%s\","
        "\"required_api_version\":%u},"
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
        static_cast<unsigned>(api::version),
        board::current.name,
        identity::hostname(),
        diagnostics::resetReason(),
        millis(),
        ESP.getFreeHeap(),
        static_cast<unsigned>(registry_store::recordCount()),
        static_cast<unsigned long>(registry_store::generation()),
        status::setupRequired() ? "true" : "false",
        status::setupActive() ? "true" : "false",
        static_cast<unsigned long>(status::setupRemainingSeconds()),
        configuration_store::ready() ? "true" : "false",
        static_cast<unsigned long>(configuration_store::settingsGeneration()),
        static_cast<unsigned long>(configuration_store::authenticationGeneration()),
        static_cast<unsigned long>(configuration_store::secretsGeneration()),
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
        web_ui::stateName(),
        web_ui::version(),
        static_cast<unsigned>(web_ui::requiredApiVersion()),
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

void handleInfo(AsyncWebServerRequest* request) {
    const auto settings = configuration_store::settings();
    JsonDocument document;
    document["firmware_version"] = firmware::version;
    document["api_version"] = api::version;
    document["display_name"] = String(
        settings.displayName, settings.displayNameLength);
    document["ui"]["state"] = web_ui::stateName();
    document["ui"]["version"] = web_ui::version();
    document["ui"]["required_api_version"] = web_ui::requiredApiVersion();
    document["board"] = board::current.name;
    document["hostname"] = identity::hostname();
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
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
    setupMutex = xSemaphoreCreateMutex();
    managementMutex = xSemaphoreCreateMutex();
    telemetrySocket.onEvent(handleWebSocketEvent);
    telemetrySocket.handleHandshake(authorizeTelemetryStream);
    server.addHandler(&telemetrySocket);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/api/v1/info", HTTP_GET, handleInfo);
    server.on("/api/v1/setup", HTTP_GET, handleSetupStatus);
    auto& setupHandler = server.on("/api/v1/setup", HTTP_POST, handleInitialSetup);
    setupHandler.setMaxContentLength(1024);
    auto& loginHandler = server.on(
        "/api/v1/session", HTTP_POST, handleLogin);
    loginHandler.setMaxContentLength(512);
    server.on("/api/v1/session", HTTP_GET, handleCurrentSession);
    server.on("/api/v1/session", HTTP_DELETE, handleLogout);
    server.on("/api/v1/nodes", HTTP_GET, handleNodes);
    server.on("/api/v1/settings", HTTP_GET, handleSettings);
    auto& settingsHandler = server.on(
        "/api/v1/settings", HTTP_PUT, handleUpdateSettings);
    settingsHandler.setMaxContentLength(1024);
    server.on("/api/v1/tokens", HTTP_GET, handleTokens);
    auto& createTokenHandler = server.on(
        "/api/v1/tokens", HTTP_POST, handleCreateToken);
    createTokenHandler.setMaxContentLength(512);
    auto& deleteTokenHandler = server.on(
        "/api/v1/tokens", HTTP_DELETE, handleDeleteToken);
    deleteTokenHandler.setMaxContentLength(128);
    auto& openPairingHandler = server.on(
        "/api/v1/pairing/open", HTTP_POST, handleOpenPairing);
    openPairingHandler.setMaxContentLength(256);
    server.on("/api/v1/pairing/close", HTTP_POST, handleClosePairing);
    server.on("/telemetry/last", HTTP_GET, handleLastTelemetry);
    web_ui::addRoutes(server);
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (!request->url().startsWith("/api/") &&
            web_ui::handlePageRequest(request)) return;
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
