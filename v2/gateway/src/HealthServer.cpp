#include "HealthServer.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ESP.h>
#include <GatewayStream.h>
#include <JoinRequest.h>
#include <UserManagement.h>
#include <ArduinoJson.h>
#include <esp_random.h>

#include <atomic>
#include <memory>
#include <new>

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
#include "PasswordHashService.h"
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

// A stream client names itself with the `X-Client` handshake header. That is
// the only honest source for the name: an API key's name is free text that
// somebody typed and the gateway accepts duplicates of it, so it says nothing
// about who is on the socket. A client that sends no header stays unnamed
// rather than being guessed at.
//
// Slot id 0 means free; AsyncWebSocket hands out ids from 1. Slots are keyed by
// client id and confirmed against the socket when read, so a missed disconnect
// leaks a slot but never a wrong name.
constexpr size_t kStreamClientNameSize = 32;

struct StreamClientIdentity {
    uint32_t id;
    char name[kStreamClientNameSize + 1];
};

StreamClientIdentity streamClients[kMaximumWebSocketClients]{};

std::atomic<uint32_t> websocketConnections{0};
std::atomic<uint32_t> websocketMessagesSent{0};
std::atomic<uint32_t> websocketMessagesDropped{0};
SemaphoreHandle_t setupMutex = nullptr;
SemaphoreHandle_t managementMutex = nullptr;
constexpr uint32_t kPasswordIterations = 100000;
constexpr const char* kSessionCookieName = "rs_session";

void sendError(AsyncWebServerRequest* request, int status, const char* code);

bool hashPassword(
    const char* password, const size_t passwordLength,
    radiosensors::user_management::PasswordCredential& credential) {
    if (password == nullptr || passwordLength < 8 || passwordLength > 128)
        return false;
    credential.algorithm =
        radiosensors::gateway_storage::PasswordHashAlgorithm::Pbkdf2HmacSha256;
    credential.iterations = kPasswordIterations;
    esp_fill_random(credential.salt, sizeof(credential.salt));
    return password_hash::computePbkdf2Sha256(
        password, passwordLength,
        credential.salt, sizeof(credential.salt), credential.iterations,
        credential.hash, sizeof(credential.hash));
}

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

// The registry and the stream share one scope: a consumer needs both to be of
// any use, so gating them separately only produced tokens that authenticate
// here and fail at /ws, or the reverse.
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
                radiosensors::gateway_storage::TokenScope::TelemetryRead)) {
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

constexpr uint32_t kRestartDelayMs = 500;

uint8_t randomOperationalNetworkId() {
    uint8_t value = 0;
    do value = static_cast<uint8_t>(esp_random());
    while (value == 0);
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
    const char* hostname = object["hostname"].is<const char*>()
        ? object["hostname"].as<const char*>() : "";
    const size_t usernameLength = username == nullptr ? 0 : strlen(username);
    const size_t passwordLength = password == nullptr ? 0 : strlen(password);
    const size_t hostnameLength = strlen(hostname);
    if (!radiosensors::user_management::validUsername(username, usernameLength) ||
        passwordLength < 8 || passwordLength > 128 ||
        hostnameLength > radiosensors::gateway_storage::kHostnameSize) {
        xSemaphoreGive(setupMutex);
        sendError(request, 422, "invalid_setup_values");
        return;
    }

    auto secrets = configuration_store::secrets();
    if (!secrets.installationKeyPresent) {
        secrets.installationKeyPresent = true;
        esp_fill_random(secrets.installationKey,
                        radiosensors::gateway_storage::kRadioKeySize);
    }
    // Zero is not a usable network id, so generate one whenever the stored
    // value is unset rather than only alongside a fresh installation key. A
    // record that already carried a key but no id would otherwise keep zero
    // for good, because setup runs exactly once.
    if (secrets.operationalNetworkId == 0) {
        secrets.operationalNetworkId = randomOperationalNetworkId();
    }
    if (object["operational_network_id"].is<uint8_t>()) {
        const uint8_t requested = object["operational_network_id"].as<uint8_t>();
        if (requested == 0) {
            xSemaphoreGive(setupMutex);
            sendError(request, 422, "invalid_operational_network_id");
            return;
        }
        secrets.operationalNetworkId = requested;
    }

    auto settings = configuration_store::settings();
    memset(settings.hostname, 0, sizeof(settings.hostname));
    settings.hostnameLength = static_cast<uint8_t>(hostnameLength);
    memcpy(settings.hostname, hostname, hostnameLength);

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
    const bool hashResult = password_hash::computePbkdf2Sha256(
        password, passwordLength,
        user.salt, sizeof(user.salt), user.pbkdf2Iterations,
        user.passwordHash, sizeof(user.passwordHash));
    if (!hashResult) {
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
    const std::unique_ptr<registry_store::Snapshot> registry(
        new (std::nothrow) registry_store::Snapshot());
    if (!registry || !registry_store::snapshot(*registry)) {
        sendError(request, 503, "registry_unavailable");
        return;
    }
    AsyncResponseStream* response =
        request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"registry_generation\":%lu,\"nodes\":[",
        static_cast<unsigned long>(registry->generation));
    for (size_t index = 0; index < registry->count; ++index) {
        const auto& node = registry->records[index];
        if (index != 0) response->print(',');
        JsonDocument document;
        document["node_id"] = node.nodeId;
        char uid[radiosensors::protocol::kDeviceUidSize * 2 + 1]{};
        for (size_t uidIndex = 0; uidIndex < sizeof(node.deviceUid); ++uidIndex)
            snprintf(uid + uidIndex * 2, 3, "%02X", node.deviceUid[uidIndex]);
        document["device_uid"] = uid;
        document["display_name"] = String(
            node.displayName, node.displayNameLength);
        document["profile_id"] = node.profileId;
        char firmware[16]{};
        snprintf(firmware, sizeof(firmware), "%u.%u.%u", node.firmware.major,
                 node.firmware.minor, node.firmware.patch);
        document["firmware"] = firmware;
        document["state"] = nodeStateName(node.state);
        telemetry_store::Record telemetry{};
        if (telemetry_store::find(node.nodeId, telemetry)) {
            document["last_seen_at_ms"] = telemetry.receivedAtUnixMs;
            document["rssi"] = telemetry.rssi;
            document["has_telemetry"] = true;
        } else {
            document["has_telemetry"] = false;
        }
        serializeJson(document, *response);
    }
    response->print("]}");
    request->send(response);
}

void handleRenameNode(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    const JsonObjectConst object = json.as<JsonObjectConst>();
    if (!object["node_id"].is<uint8_t>() ||
        !object["display_name"].is<const char*>()) {
        sendError(request, 422, "invalid_node_values");
        return;
    }
    const uint8_t nodeId = object["node_id"].as<uint8_t>();
    const char* const displayName = object["display_name"].as<const char*>();
    const size_t length = strlen(displayName);
    radiosensors::registry::RenameStatus renameStatus{};
    const RegistryCommitStatus commit = registry_store::renameAndSave(
        nodeId, displayName, length, renameStatus);
    if (renameStatus == radiosensors::registry::RenameStatus::InvalidName) {
        sendError(request, 422, "invalid_display_name");
    } else if (renameStatus == radiosensors::registry::RenameStatus::NotFound) {
        sendError(request, 404, "node_not_found");
    } else if (commit == RegistryCommitStatus::StorageError) {
        sendError(request, 500, "node_storage_failed");
    } else if (commit == RegistryCommitStatus::NotInitialized) {
        sendError(request, 503, "registry_unavailable");
    } else {
        JsonDocument response;
        response["node_id"] = nodeId;
        response["display_name"] = displayName;
        response["registry_generation"] = registry_store::generation();
        String body;
        serializeJson(response, body);
        request->send(200, "application/json", body);
    }
}

void handleDeleteNode(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>() ||
        !json.as<JsonObjectConst>()["node_id"].is<uint8_t>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    const uint8_t nodeId = json.as<JsonObjectConst>()["node_id"].as<uint8_t>();
    bool removed = false;
    const RegistryCommitStatus commit =
        registry_store::removeAndSave(nodeId, removed);
    if (commit == RegistryCommitStatus::StorageError) {
        sendError(request, 500, "node_storage_failed");
    } else if (commit == RegistryCommitStatus::NotInitialized) {
        sendError(request, 503, "registry_unavailable");
    } else if (!removed) {
        sendError(request, 404, "node_not_found");
    } else {
        telemetry_store::erase(nodeId);
        request->send(204);
    }
}

// Set when a reset has been acknowledged; loop() performs the restart so the
// response leaves the socket first. Restarting inside the handler would drop
// the connection and leave the caller unable to tell success from failure.
uint32_t restartAtMs = 0;

void handleResetRadioNetwork(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;

    // The body is optional: without one the gateway picks the network id.
    uint8_t requestedNetworkId = 0;
    if (json.is<JsonObject>()) {
        const JsonObjectConst object = json.as<JsonObjectConst>();
        if (!object["operational_network_id"].isNull()) {
            if (!object["operational_network_id"].is<uint8_t>() ||
                object["operational_network_id"].as<uint8_t>() == 0) {
                sendError(request, 422, "invalid_operational_network_id");
                return;
            }
            requestedNetworkId = object["operational_network_id"].as<uint8_t>();
        }
    }

    // Clear the registry first: the nodes in it are bound to the old network
    // and key, and saveSecrets refuses to change the radio profile while any
    // of them is still active.
    size_t removedNodes = 0;
    const RegistryCommitStatus registryStatus = registry_store::clearAndSave(removedNodes);
    if (registryStatus == RegistryCommitStatus::StorageError) {
        sendError(request, 500, "node_storage_failed");
        return;
    }
    if (registryStatus == RegistryCommitStatus::NotInitialized) {
        sendError(request, 503, "registry_unavailable");
        return;
    }
    telemetry_store::clear();

    auto secrets = configuration_store::secrets();
    secrets.installationKeyPresent = true;
    esp_fill_random(secrets.installationKey,
                    radiosensors::gateway_storage::kRadioKeySize);
    secrets.operationalNetworkId = requestedNetworkId != 0
        ? requestedNetworkId
        : randomOperationalNetworkId();

    const auto secretResult = configuration_store::saveSecrets(secrets);
    if (secretResult == configuration_store::SaveStatus::LockedByActiveNodes) {
        sendError(request, 409, "secrets_locked_by_active_nodes");
        return;
    }
    if (secretResult != configuration_store::SaveStatus::Ok) {
        sendError(request, 500, "secrets_storage_failed");
        return;
    }

    JsonDocument response;
    response["operational_network_id"] = secrets.operationalNetworkId;
    response["removed_nodes"] = removedNodes;
    response["restarting"] = true;
    String body;
    serializeJson(response, body);
    request->send(202, "application/json", body);
    restartAtMs = millis() + kRestartDelayMs;
}

// `scopes` is optional. Omitting it grants the only scope there is, which is
// what every caller wants and what the Web UI now sends. A field that is
// present is still validated: a client asking for a scope this firmware does
// not have is told so rather than quietly handed the one it does have.
bool parseTokenScopes(const JsonVariantConst& json, uint16_t& scopes) {
    scopes = radiosensors::gateway_storage::TokenScope::TelemetryRead;
    if (json.isNull()) return true;
    scopes = 0;
    if (!json.is<JsonArrayConst>()) return false;
    for (const JsonVariantConst item : json.as<JsonArrayConst>()) {
        if (!item.is<const char*>()) return false;
        const char* value = item.as<const char*>();
        if (strcmp(value, "telemetry:read") != 0) return false;
        scopes |= radiosensors::gateway_storage::TokenScope::TelemetryRead;
    }
    return scopes != 0;
}

void addTokenScopes(JsonArray output, const uint16_t scopes) {
    if ((scopes & radiosensors::gateway_storage::TokenScope::TelemetryRead) != 0)
        output.add("telemetry:read");
}

void addUser(JsonObject output,
             const radiosensors::gateway_storage::UserRecord& source) {
    output["id"] = source.id;
    output["username"] = String(source.username, source.usernameLength);
    output["role"] = roleName(source.role);
    output["enabled"] = source.enabled;
}

bool parseUserRole(const JsonVariantConst value,
                   radiosensors::gateway_storage::UserRole& role) {
    if (!value.is<const char*>()) return false;
    const char* name = value.as<const char*>();
    if (strcmp(name, "admin") == 0) {
        role = radiosensors::gateway_storage::UserRole::Admin;
        return true;
    }
    if (strcmp(name, "viewer") == 0) {
        role = radiosensors::gateway_storage::UserRole::Viewer;
        return true;
    }
    return false;
}

void sendUserMutationError(
    AsyncWebServerRequest* request,
    const radiosensors::user_management::Status status) {
    using radiosensors::user_management::Status;
    switch (status) {
        case Status::InvalidValue:
            sendError(request, 422, "invalid_user_values");
            break;
        case Status::NotFound:
            sendError(request, 404, "user_not_found");
            break;
        case Status::UsernameExists:
            sendError(request, 409, "username_already_exists");
            break;
        case Status::CapacityReached:
            sendError(request, 409, "user_capacity_reached");
            break;
        case Status::LastAdminRequired:
            sendError(request, 409, "last_admin_required");
            break;
        case Status::Ok:
            break;
    }
}

void handleUsers(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, false)) return;
    const auto data = configuration_store::authentication();
    JsonDocument document;
    JsonArray users = document["users"].to<JsonArray>();
    for (uint8_t index = 0; index < data.userCount; ++index)
        addUser(users.add<JsonObject>(), data.users[index]);
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
}

void handleCreateUser(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    const JsonObject object = json.as<JsonObject>();
    const char* username = object["username"].is<const char*>()
        ? object["username"].as<const char*>() : nullptr;
    const char* password = object["password"].is<const char*>()
        ? object["password"].as<const char*>() : nullptr;
    const size_t usernameLength = username == nullptr ? 0 : strlen(username);
    const size_t passwordLength = password == nullptr ? 0 : strlen(password);
    radiosensors::gateway_storage::UserRole role{};
    if (!radiosensors::user_management::validUsername(username, usernameLength) ||
        passwordLength < 8 ||
        passwordLength > 128 || !parseUserRole(object["role"], role) ||
        (!object["enabled"].isNull() && !object["enabled"].is<bool>())) {
        sendError(request, 422, "invalid_user_values");
        return;
    }
    if (managementMutex == nullptr ||
        xSemaphoreTake(managementMutex, 0) != pdTRUE) {
        sendError(request, 409, "mutation_busy");
        return;
    }
    auto data = configuration_store::authentication();
    radiosensors::user_management::PasswordCredential credential{};
    if (!hashPassword(password, passwordLength, credential)) {
        xSemaphoreGive(managementMutex);
        sendError(request, 500, "password_hash_failed");
        return;
    }
    uint32_t createdId = 0;
    const auto mutation = radiosensors::user_management::create(
        data, username, usernameLength, role, object["enabled"] | true,
        credential, createdId);
    (void)createdId;
    memset(&credential, 0, sizeof(credential));
    if (mutation != radiosensors::user_management::Status::Ok) {
        xSemaphoreGive(managementMutex);
        sendUserMutationError(request, mutation);
        return;
    }
    const auto saved = configuration_store::saveAuthentication(data);
    xSemaphoreGive(managementMutex);
    if (saved != configuration_store::SaveStatus::Ok) {
        sendError(request, 500, "user_storage_failed");
        return;
    }
    const auto& user = data.users[data.userCount - 1];
    JsonDocument document;
    addUser(document.to<JsonObject>(), user);
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->setCode(201);
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
}

void handleUpdateUser(AsyncWebServerRequest* request, JsonVariant& json) {
    authentication::Principal principal{};
    if (!authorizeAdmin(request, principal, true)) return;
    if (!json.is<JsonObject>() || !json["id"].is<uint32_t>()) {
        sendError(request, 400, "invalid_request");
        return;
    }
    const JsonObject object = json.as<JsonObject>();
    const char* username = object["username"].is<const char*>()
        ? object["username"].as<const char*>() : nullptr;
    const size_t usernameLength = username == nullptr ? 0 : strlen(username);
    radiosensors::gateway_storage::UserRole role{};
    const bool hasPassword = !object["password"].isNull();
    const char* password = hasPassword && object["password"].is<const char*>()
        ? object["password"].as<const char*>() : nullptr;
    const size_t passwordLength = password == nullptr ? 0 : strlen(password);
    if (!radiosensors::user_management::validUsername(username, usernameLength) ||
        !parseUserRole(object["role"], role) || !object["enabled"].is<bool>() ||
        (hasPassword && (passwordLength < 8 || passwordLength > 128))) {
        sendError(request, 422, "invalid_user_values");
        return;
    }
    if (managementMutex == nullptr ||
        xSemaphoreTake(managementMutex, 0) != pdTRUE) {
        sendError(request, 409, "mutation_busy");
        return;
    }
    auto data = configuration_store::authentication();
    const uint32_t id = object["id"].as<uint32_t>();
    radiosensors::user_management::PasswordCredential credential{};
    if (hasPassword && !hashPassword(password, passwordLength, credential)) {
        xSemaphoreGive(managementMutex);
        sendError(request, 500, "password_hash_failed");
        return;
    }
    const auto mutation = radiosensors::user_management::update(
        data, id, username, usernameLength, role, object["enabled"].as<bool>(),
        hasPassword ? &credential : nullptr);
    memset(&credential, 0, sizeof(credential));
    if (mutation != radiosensors::user_management::Status::Ok) {
        xSemaphoreGive(managementMutex);
        sendUserMutationError(request, mutation);
        return;
    }
    const auto saved = configuration_store::saveAuthentication(data);
    xSemaphoreGive(managementMutex);
    if (saved != configuration_store::SaveStatus::Ok &&
        saved != configuration_store::SaveStatus::NoChange) {
        sendError(request, 500, "user_storage_failed");
        return;
    }
    authentication::invalidateUserSessions(id);
    uint8_t index = 0;
    while (data.users[index].id != id) ++index;
    const auto& user = data.users[index];
    JsonDocument document;
    addUser(document.to<JsonObject>(), user);
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
}

void handleDeleteUser(AsyncWebServerRequest* request, JsonVariant& json) {
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
    auto data = configuration_store::authentication();
    const uint32_t id = json["id"].as<uint32_t>();
    const auto mutation = radiosensors::user_management::remove(data, id);
    if (mutation != radiosensors::user_management::Status::Ok) {
        xSemaphoreGive(managementMutex);
        sendUserMutationError(request, mutation);
        return;
    }
    const auto saved = configuration_store::saveAuthentication(data);
    xSemaphoreGive(managementMutex);
    if (saved != configuration_store::SaveStatus::Ok) {
        sendError(request, 500, "user_storage_failed");
        return;
    }
    authentication::invalidateUserSessions(id);
    request->send(204);
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
    document["hostname"] = String(settings.hostname, settings.hostnameLength);
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
    const char* hostname = object["hostname"].is<const char*>()
        ? object["hostname"].as<const char*>() : nullptr;
    const size_t hostnameLength = hostname == nullptr ? 0 : strlen(hostname);
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
        hostnameLength > radiosensors::gateway_storage::kHostnameSize ||
        pairingSeconds < 30 || pairingSeconds > 900 ||
        setupSeconds < 60 || setupSeconds > 1800 ||
        servers.size() > radiosensors::gateway_storage::kNtpServerCount) {
        xSemaphoreGive(managementMutex);
        sendError(request, 422, "invalid_settings_values");
        return;
    }

    auto settings = configuration_store::settings();
    memset(settings.hostname, 0, sizeof(settings.hostname));
    settings.hostnameLength = static_cast<uint8_t>(hostnameLength);
    memcpy(settings.hostname, hostname, hostnameLength);
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

// The name is written by whoever connects and is echoed into JSON and into the
// Web UI, so only printable ASCII survives, and the two characters that would
// break out of a JSON string never do.
void rememberStreamClient(const uint32_t id, const char* name) {
    for (StreamClientIdentity& slot : streamClients) {
        if (slot.id != 0 && slot.id != id) continue;
        slot.id = id;
        size_t written = 0;
        for (size_t index = 0;
             name != nullptr && name[index] != '\0' &&
             written < kStreamClientNameSize;
             ++index) {
            const char value = name[index];
            if (value < 0x20 || value >= 0x7F || value == '"' || value == '\\')
                continue;
            slot.name[written++] = value;
        }
        slot.name[written] = '\0';
        return;
    }
}

void forgetStreamClient(const uint32_t id) {
    for (StreamClientIdentity& slot : streamClients) {
        if (slot.id != id) continue;
        slot.id = 0;
        slot.name[0] = '\0';
        return;
    }
}

void handleWebSocketEvent(
    AsyncWebSocket*,
    AsyncWebSocketClient* client,
    const AwsEventType type,
    void* argument,
    uint8_t*,
    size_t) {
    if (type == WS_EVT_CONNECT) {
        ++websocketConnections;
        // The library holds the handshake request alive for this callback and
        // passes it here, so the identity is read where the client is known
        // and no state has to be carried over from handleHandshake.
        const auto* request = static_cast<AsyncWebServerRequest*>(argument);
        const char* name = nullptr;
        String header;
        if (request != nullptr && request->hasHeader("X-Client")) {
            header = request->getHeader("X-Client")->value();
            name = header.c_str();
        }
        rememberStreamClient(client->id(), name);
        client->setCloseClientOnQueueFull(true);
        client->keepAlivePeriod(kWebSocketKeepAliveSeconds);
        sendSnapshot(client);
    } else if (type == WS_EVT_DISCONNECT) {
        forgetStreamClient(client->id());
    }
}

// Session-only, unlike the client count in `/health`: the count says something
// is reading, which mDNS discovery already implies, while the names say which
// products this installation runs. That belongs behind a login.
void handleClients(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeSession(request, principal, false)) return;
    JsonDocument document;
    JsonArray clients = document["clients"].to<JsonArray>();
    for (const StreamClientIdentity& slot : streamClients) {
        // Confirmed against the socket, so a slot left behind by a missed
        // disconnect is skipped rather than reported as a live client.
        if (slot.id == 0 || !telemetrySocket.hasClient(slot.id)) continue;
        JsonObject entry = clients.add<JsonObject>();
        entry["id"] = slot.id;
        entry["name"] = slot.name;
    }
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
}

// The public liveness probe, and deliberately almost empty. Everything a
// person would want to see about the gateway lives behind a session at
// /ui/status; this exists so something on the network can tell the gateway
// is up without a credential, and so RadioResetDialog can watch for a reboot
// after a reset has killed every session.
//
// `boot_id` is the one detail worth publishing here: it changes on every boot,
// which is what makes "it came back" distinguishable from "it never went
// down", and it is already broadcast in the mDNS TXT record anyway.
void handleHealth(AsyncWebServerRequest* request) {
    char body[96]{};
    snprintf(
        body, sizeof(body), "{\"status\":\"ok\",\"boot_id\":\"%s\"}",
        identity::bootId());
    AsyncWebServerResponse* const response =
        request->beginResponse(200, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// Everything the Web UI shows about the gateway. Session-only: it reports the
// radio pinout, MAC, counters, free heap and storage generations, which are
// diagnostics for whoever runs the gateway, not facts for the network.
void handleStatus(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeSession(request, principal, false)) return;
    const radio::Snapshot radioSnapshot = radio::snapshot();
    const commissioning::Snapshot commissioningSnapshot = commissioning::snapshot();
    const telemetry_store::Snapshot telemetrySnapshot = telemetry_store::snapshot();
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->printf(
        "{\"status\":\"ok\",\"firmware\":\"%s\",\"api_version\":%u,\"board\":\"%s\",\"hostname\":\"%s\","
        "\"gateway_id\":\"%s\",\"boot_id\":\"%s\","
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
        "\"required_firmware\":\"%s\"},"
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
        identity::gatewayId(),
        identity::bootId(),
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
        web_ui::requiredFirmware(),
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
    JsonDocument document;
    document["firmware_version"] = firmware::version;
    document["api_version"] = api::version;
    document["ui"]["state"] = web_ui::stateName();
    document["ui"]["version"] = web_ui::version();
    document["ui"]["required_firmware"] = web_ui::requiredFirmware();
    document["board"] = board::current.name;
    document["hostname"] = identity::hostname();
    document["gateway_id"] = identity::gatewayId();
    document["boot_id"] = identity::bootId();
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    serializeJson(document, *response);
    request->send(response);
}

// A bench diagnostic that predates the WebSocket: it returns a real decoded
// telemetry payload, which is exactly what `telemetry:read` exists to protect,
// so it takes a session. Deliberately not a bearer endpoint -- it is not part
// of the external client contract, and a client wanting telemetry uses /ws.
void handleLastTelemetry(AsyncWebServerRequest* request) {
    authentication::Principal principal{};
    if (!authorizeSession(request, principal, false)) return;
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

// REGISTRY_CHANGED, specified in WEBSOCKET.md. Polled rather than raised from
// each commit because a commit holds the registry mutex, so a callback from
// there would broadcast under that lock. The generation read is lock-free.
void broadcastRegistryChanges() {
    static uint32_t lastGeneration = 0;
    static bool primed = false;
    const uint32_t generation = registry_store::generation();
    // Boot is not a change, and a client that connects gets the registry anyway.
    if (!primed) {
        lastGeneration = generation;
        primed = true;
        return;
    }
    if (generation == lastGeneration) return;
    // Recorded before the no-clients check, so reconnecting does not replay it.
    lastGeneration = generation;
    if (telemetrySocket.count() == 0) return;
    uint8_t message[radiosensors::stream::kRegistryChangedFrameSize]{};
    const size_t size = radiosensors::stream::encodeRegistryChanged(
        telemetry_store::snapshot().updates, generation, message, sizeof(message));
    const AsyncWebSocket::SendStatus status = telemetrySocket.binaryAll(message, size);
    if (status == AsyncWebSocket::DISCARDED ||
        status == AsyncWebSocket::PARTIALLY_ENQUEUED) {
        ++websocketMessagesDropped;
    }
    if (status != AsyncWebSocket::DISCARDED) ++websocketMessagesSent;
}

}  // namespace

void begin() {
    setupMutex = xSemaphoreCreateMutex();
    managementMutex = xSemaphoreCreateMutex();
    telemetrySocket.onEvent(handleWebSocketEvent);
    telemetrySocket.handleHandshake(authorizeTelemetryStream);
    server.addHandler(&telemetrySocket);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/ui/status", HTTP_GET, handleStatus);
    server.on("/api/info", HTTP_GET, handleInfo);
    server.on("/ui/setup", HTTP_GET, handleSetupStatus);
    auto& setupHandler = server.on("/ui/setup", HTTP_POST, handleInitialSetup);
    setupHandler.setMaxContentLength(1024);
    auto& loginHandler = server.on(
        "/ui/session", HTTP_POST, handleLogin);
    loginHandler.setMaxContentLength(512);
    server.on("/ui/session", HTTP_GET, handleCurrentSession);
    server.on("/ui/session", HTTP_DELETE, handleLogout);
    server.on("/ui/clients", HTTP_GET, handleClients);
    server.on("/api/nodes", HTTP_GET, handleNodes);
    auto& renameNodeHandler = server.on(
        "/ui/nodes", HTTP_PATCH, handleRenameNode);
    renameNodeHandler.setMaxContentLength(256);
    auto& deleteNodeHandler = server.on(
        "/ui/nodes", HTTP_DELETE, handleDeleteNode);
    deleteNodeHandler.setMaxContentLength(128);
    server.on("/ui/settings", HTTP_GET, handleSettings);
    auto& settingsHandler = server.on(
        "/ui/settings", HTTP_PUT, handleUpdateSettings);
    settingsHandler.setMaxContentLength(1024);
    server.on("/ui/users", HTTP_GET, handleUsers);
    auto& createUserHandler = server.on(
        "/ui/users", HTTP_POST, handleCreateUser);
    createUserHandler.setMaxContentLength(512);
    auto& updateUserHandler = server.on(
        "/ui/users", HTTP_PUT, handleUpdateUser);
    updateUserHandler.setMaxContentLength(512);
    auto& deleteUserHandler = server.on(
        "/ui/users", HTTP_DELETE, handleDeleteUser);
    deleteUserHandler.setMaxContentLength(128);
    server.on("/ui/tokens", HTTP_GET, handleTokens);
    auto& createTokenHandler = server.on(
        "/ui/tokens", HTTP_POST, handleCreateToken);
    createTokenHandler.setMaxContentLength(512);
    auto& deleteTokenHandler = server.on(
        "/ui/tokens", HTTP_DELETE, handleDeleteToken);
    deleteTokenHandler.setMaxContentLength(128);
    auto& resetRadioHandler = server.on(
        "/ui/radio/reset", HTTP_POST, handleResetRadioNetwork);
    resetRadioHandler.setMaxContentLength(128);
    auto& openPairingHandler = server.on(
        "/ui/pairing/open", HTTP_POST, handleOpenPairing);
    openPairingHandler.setMaxContentLength(256);
    server.on("/ui/pairing/close", HTTP_POST, handleClosePairing);
    server.on("/ui/telemetry/last", HTTP_GET, handleLastTelemetry);
    web_ui::addRoutes(server);
    // A mistyped API path must answer with JSON, not with the single-page
    // application, or a typo looks like a working request that returned HTML.
    server.onNotFound([](AsyncWebServerRequest* request) {
        const String url = request->url();
        if (!url.startsWith("/api/") && !url.startsWith("/ui/") &&
            web_ui::handlePageRequest(request)) return;
        request->send(404, "application/json", "{\"error\":\"not_found\"}");
    });
    server.begin();
    Serial.println("Health server listening on port 80");
}

void loop() {
    telemetrySocket.cleanupClients(kMaximumWebSocketClients);
    broadcastRegistryChanges();
    if (restartAtMs != 0 && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
        Serial.println("Radio network reset: restarting");
        Serial.flush();
        ESP.restart();
    }
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
