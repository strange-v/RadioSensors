#include "WebUiService.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "ApiVersion.h"
#include "FirmwareVersion.h"

namespace gateway::web_ui {
namespace {

constexpr char kPartitionLabel[] = "web";
constexpr char kManifestPath[] = "/ui-manifest.json";
constexpr size_t kMaximumManifestBytes = 1024;
constexpr size_t kMaximumVersionLength = 31;

State currentState = State::Unavailable;
char currentVersion[kMaximumVersionLength + 1]{};
uint16_t currentRequiredApiVersion = 0;
bool mounted = false;

constexpr char kRecoveryPage[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RadioSensors gateway</title><style>body{margin:0;background:#0f172a;color:#e2e8f0;font:16px system-ui,sans-serif}main{max-width:42rem;margin:12vh auto;padding:2rem}section{background:#1e293b;border:1px solid #334155;border-radius:1rem;padding:2rem}h1{margin-top:0;font-size:1.5rem}p{line-height:1.6;color:#cbd5e1}code{color:#7dd3fc}</style></head>
<body><main><section><h1>Web UI unavailable</h1><p>The installed Web UI is missing, damaged, or incompatible with this gateway firmware.</p><p>Install a compatible LittleFS image, then reload this page.</p><p>Firmware: <code>%FIRMWARE%</code> &middot; API: <code>%API%</code> &middot; UI state: <code>%STATE%</code></p></section></main></body></html>)html";

void clearManifest() {
    currentVersion[0] = '\0';
    currentRequiredApiVersion = 0;
}

void inspectManifest() {
    clearManifest();
    if (!LittleFS.exists(kManifestPath)) {
        currentState = State::MissingManifest;
        return;
    }

    File manifest = LittleFS.open(kManifestPath, "r");
    if (!manifest || manifest.size() == 0 || manifest.size() > kMaximumManifestBytes) {
        currentState = State::InvalidManifest;
        return;
    }

    JsonDocument document;
    if (deserializeJson(document, manifest) != DeserializationError::Ok) {
        currentState = State::InvalidManifest;
        return;
    }

    const char* uiVersion = document["ui_version"].as<const char*>();
    if (uiVersion == nullptr || uiVersion[0] == '\0' ||
        strlen(uiVersion) > kMaximumVersionLength ||
        !document["api_version"].is<uint16_t>() ||
        !LittleFS.exists("/index.html")) {
        currentState = State::InvalidManifest;
        return;
    }

    strlcpy(currentVersion, uiVersion, sizeof(currentVersion));
    currentRequiredApiVersion = document["api_version"].as<uint16_t>();
    currentState = currentRequiredApiVersion == api::version
        ? State::Ready
        : State::Incompatible;
}

bool acceptsHtml(AsyncWebServerRequest* request) {
    if (!request->hasHeader("Accept")) return false;
    const String value = request->getHeader("Accept")->value();
    return value.indexOf("text/html") >= 0;
}

}  // namespace

void begin() {
    mounted = LittleFS.begin(false, "/littlefs", 10, kPartitionLabel);
    if (!mounted) {
        currentState = State::Unavailable;
        clearManifest();
        Serial.println("Web UI: LittleFS unavailable");
        return;
    }
    inspectManifest();
    Serial.printf("Web UI: %s%s%s\n", stateName(),
                  currentVersion[0] == '\0' ? "" : ", version=",
                  currentVersion);
}

void addRoutes(AsyncWebServer& server) {
    if (currentState == State::Ready) {
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    }
}

void prepareForFilesystemUpdate() {
    currentState = State::Updating;
    clearManifest();
    if (mounted) {
        LittleFS.end();
        mounted = false;
    }
}

void recoverAfterFailedFilesystemUpdate() {
    begin();
}

bool handlePageRequest(AsyncWebServerRequest* request) {
    if (request->method() != HTTP_GET || !acceptsHtml(request)) return false;
    if (currentState == State::Ready && mounted) {
        AsyncWebServerResponse* response =
            request->beginResponse(LittleFS, "/index.html", "text/html");
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    } else {
        sendRecoveryPage(request);
    }
    return true;
}

State state() { return currentState; }

const char* stateName() {
    switch (currentState) {
        case State::Unavailable: return "unavailable";
        case State::MissingManifest: return "missing_manifest";
        case State::InvalidManifest: return "invalid_manifest";
        case State::Incompatible: return "incompatible";
        case State::Ready: return "ready";
        case State::Updating: return "updating";
    }
    return "unknown";
}

const char* version() { return currentVersion; }
uint16_t requiredApiVersion() { return currentRequiredApiVersion; }

void sendRecoveryPage(AsyncWebServerRequest* request) {
    String page{kRecoveryPage};
    page.replace("%FIRMWARE%", firmware::version);
    page.replace("%API%", String(api::version));
    page.replace("%STATE%", stateName());
    AsyncWebServerResponse* response = request->beginResponse(503, "text/html", page);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

}  // namespace gateway::web_ui
