#include "WebUiService.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "FirmwareVersion.h"

namespace gateway::web_ui {
namespace {

constexpr char kPartitionLabel[] = "web";
constexpr char kManifestPath[] = "/ui-manifest.json";
constexpr char kIndexPath[] = "/index.html";
constexpr char kIndexGzipPath[] = "/index.html.gz";

// The build ships index.html gzipped; AsyncFileResponse serves the .gz variant
// transparently, so either name counts as a present page.
bool indexPresent() {
    return LittleFS.exists(kIndexPath) || LittleFS.exists(kIndexGzipPath);
}
constexpr size_t kMaximumManifestBytes = 1024;
constexpr size_t kMaximumVersionLength = 31;

State currentState = State::Unavailable;
char currentVersion[kMaximumVersionLength + 1]{};
char currentRequiredFirmware[kMaximumVersionLength + 1]{};
bool mounted = false;

constexpr char kRecoveryPage[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OSK Sense Hub</title><style>body{margin:0;background:#0f172a;color:#e2e8f0;font:16px system-ui,sans-serif}main{max-width:42rem;margin:12vh auto;padding:2rem}section{background:#1e293b;border:1px solid #334155;border-radius:1rem;padding:2rem}h1{margin-top:0;font-size:1.5rem}p{line-height:1.6;color:#cbd5e1}code{color:#7dd3fc}</style></head>
<body><main><section><h1>Web UI unavailable</h1><p>The installed Web UI is missing, damaged, or incompatible with this gateway firmware.</p><p>Install a compatible LittleFS image, then reload this page.</p><p>Firmware: <code>%FIRMWARE%</code> &middot; UI state: <code>%STATE%</code></p></section></main></body></html>)html";

void clearManifest() {
    currentVersion[0] = '\0';
    currentRequiredFirmware[0] = '\0';
}

// The Web UI is flashed as its own LittleFS image, so it can be older or newer
// than the firmware beneath it. It depends on /ui/*, which moves with the
// firmware, so the gate is the firmware series rather than `api_version` --
// that number describes the client contract the UI barely touches.
//
// The patch level is ignored: it does not reshape /ui/*, and rejecting a good
// UI over one would only tempt someone to skip the check.
bool firmwareSeriesMatches(const char* const required) {
    const char* const minorDot = strchr(required, '.');
    if (minorDot == nullptr) return false;
    const char* const patchDot = strchr(minorDot + 1, '.');
    const size_t length = patchDot == nullptr
        ? strlen(required)
        : static_cast<size_t>(patchDot - required);
    return length != 0 &&
        strncmp(required, firmware::version, length) == 0 &&
        (firmware::version[length] == '\0' || firmware::version[length] == '.');
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
    const char* requiredFirmware = document["required_firmware"].as<const char*>();
    if (uiVersion == nullptr || uiVersion[0] == '\0' ||
        strlen(uiVersion) > kMaximumVersionLength ||
        requiredFirmware == nullptr || requiredFirmware[0] == '\0' ||
        strlen(requiredFirmware) > kMaximumVersionLength ||
        !indexPresent()) {
        currentState = State::InvalidManifest;
        return;
    }

    strlcpy(currentVersion, uiVersion, sizeof(currentVersion));
    strlcpy(
        currentRequiredFirmware, requiredFirmware,
        sizeof(currentRequiredFirmware));
    currentState = firmwareSeriesMatches(requiredFirmware)
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
        // Only the hashed build output is reachable over HTTP. Serving the
        // filesystem root instead would also expose /ui-manifest.json, which
        // no browser reads -- the firmware parses it locally at boot -- and
        // which carries the build SHA. Every name here contains a content
        // hash, so the response can be cached permanently; index.html is sent
        // by handlePageRequest with its own revalidating header.
        server.serveStatic("/assets/", LittleFS, "/assets/")
            .setCacheControl("public, max-age=31536000, immutable");
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
            request->beginResponse(LittleFS, kIndexPath, "text/html");
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
const char* requiredFirmware() { return currentRequiredFirmware; }

void sendRecoveryPage(AsyncWebServerRequest* request) {
    String page{kRecoveryPage};
    page.replace("%FIRMWARE%", firmware::version);
    page.replace("%STATE%", stateName());
    AsyncWebServerResponse* response = request->beginResponse(503, "text/html", page);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

}  // namespace gateway::web_ui
