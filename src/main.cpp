#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <mutex>
#include "jenkins.h"

// ---- Edit before uploading ----
const char* WIFI_SSID = "SSID";
const char* WIFI_PASSWORD = "PASSWORD";
// red, orange, green. On a classic ESP32 avoid GPIO15 (strapping pin) and 16/17 on WROVER modules (PSRAM).
const uint8_t PINS[] = {25, 26, 33};
const bool ACTIVE_LOW = false;  // true for relay boards that switch on LOW

extern const char INDEX_HTML[] asm("_binary_src_index_html_start");  // embedded by board_build.embed_txtfiles

using Lock = std::lock_guard<std::mutex>;

struct Settings {
    String url, user, token;
    uint32_t pollSeconds;
};

// Shared between loop() and the web server task, guarded by mtx.
std::mutex mtx;
Settings cfg;
std::set<std::string> watched;
std::vector<Job> jobs;      // from the last successful poll
String error = "Starting";  // non-empty: the monitor itself has a problem, so the lamps blink
String manual;              // lamp test from the web UI: red, orange, green or off; empty = automatic
uint32_t lastPoll;
volatile bool pollNow = true;

Preferences prefs;
AsyncWebServer server(80);

// ---- Log: serial plus the last few KB for the web UI ----
std::mutex logMtx;
String logText;

void addLog(const char* line) {
    Lock g(logMtx);
    logText += line;
    if (logText.length() > 8192) logText.remove(0, logText.length() - 6144);
}

void logf(const char* fmt, ...) {
    char buf[256];
    int n = snprintf(buf, sizeof buf, "[%lus] ", millis() / 1000);
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + n, sizeof buf - n - 1, fmt, args);
    va_end(args);
    strcat(buf, "\n");
    Serial.print(buf);
    addLog(buf);
}

vprintf_like_t serialVprintf;

int captureIdfLog(const char* fmt, va_list args) {  // ESP-IDF logs (e.g. WiFi) also go to the web log
    char buf[256];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(buf, sizeof buf, fmt, copy);
    va_end(copy);
    addLog(buf);
    return serialVprintf(fmt, args);
}

// ---- Jenkins polling and lamps ----
String lampState() {  // caller holds mtx
    return manual.length() ? manual : error.length() ? String("error") : String(overall(jobs, watched));
}

void poll() {
    Settings s;
    {
        Lock g(mtx);
        s = cfg;
    }
    String err;
    std::vector<Job> fresh;
    if (WiFi.status() != WL_CONNECTED) {
        err = "WiFi not connected";
    } else {
        HTTPClient http;
        http.useHTTP10(true);  // no chunked encoding, so the JSON can be parsed straight off the stream
        http.begin(s.url + "/api/json?tree=" + TREE);
        if (s.user.length()) http.setAuthorization(s.user.c_str(), s.token.c_str());
        int code = http.GET();
        JsonDocument doc;
        DeserializationError e;
        if (code != HTTP_CODE_OK) {
            err = code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code);
        } else if ((e = deserializeJson(doc, http.getStream()))) {
            err = String("Bad JSON: ") + e.c_str();  // NoMemory here means the job list is too big for the heap
        } else {
            collectJobs(doc["jobs"].as<JsonArrayConst>(), fresh);
        }
        http.end();
    }
    Lock g(mtx);
    if (err != error) {
        if (err.length()) logf("Jenkins: %s", err.c_str());
        else logf("Jenkins: OK, %u jobs", fresh.size());
    }
    if (err.isEmpty()) jobs.swap(fresh);
    error = err;
    lastPoll = millis();
}

void setLamps(const String& state, bool blinkOn) {
    const char* names[] = {"red", "orange", "green"};
    for (int i = 0; i < 3; i++) {
        bool on = state == names[i] || (state == "error" && blinkOn);
        digitalWrite(PINS[i], on != ACTIVE_LOW);
    }
}

void loop() {
    static uint32_t last;
    static bool blinkOn;
    static String shown;
    if (pollNow || millis() - last >= cfg.pollSeconds * 1000) {
        pollNow = false;
        last = millis();
        poll();
    }
    String state;
    {
        Lock g(mtx);
        state = lampState();
    }
    if (state != shown) logf("Light: %s", state.c_str());
    shown = state;
    setLamps(state, blinkOn = !blinkOn);
    delay(500);
}

// ---- Web UI and API ----
bool saveWatched() {  // caller holds mtx
    String s;
    for (const auto& name : watched) s += String(name.c_str()) + "\n";
    // ponytail: NVS strings max out near 4000 bytes (~100 job names); move to a file if more are needed.
    return prefs.putString("watched", s) == s.length();
}

// Pages on other sites can't send a custom header without a CORS preflight, which is never answered: blocks CSRF.
bool fromUi(AsyncWebServerRequest* r) {
    if (r->hasHeader("X-BuildStatus")) return true;
    r->send(403, "text/plain", "Missing X-BuildStatus header");
    return false;
}

String param(AsyncWebServerRequest* r, const char* name) {
    const AsyncWebParameter* p = r->getParam(name, true);
    return p ? p->value() : String();
}

void sendJson(AsyncWebServerRequest* r, const JsonDocument& doc) {
    AsyncResponseStream* res = r->beginResponseStream("application/json");
    serializeJson(doc, *res);
    r->send(res);
}

void setupWeb() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "text/html", (const uint8_t*)INDEX_HTML, strlen(INDEX_HTML));
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        {
            Lock g(mtx);
            doc["light"] = lampState();
            doc["manual"] = manual.length() > 0;
            doc["error"] = error;
            doc["age"] = (millis() - lastPoll) / 1000;
            JsonArray w = doc["watched"].to<JsonArray>();
            for (const auto& name : watched) w.add(name);
            JsonArray list = doc["jobs"].to<JsonArray>();
            for (const Job& j : jobs) {
                JsonObject o = list.add<JsonObject>();
                o["name"] = j.name;
                o["building"] = j.building;
                o["result"] = j.result;
                o["number"] = j.number;
            }
        }
        sendJson(r, doc);
    });

    server.on("/api/watch", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (!fromUi(r)) return;
        std::string name = param(r, "name").c_str();
        if (name.empty()) return r->send(400, "text/plain", "name required");
        Lock g(mtx);
        if (param(r, "on") == "1") watched.insert(name);
        else watched.erase(name);
        if (!saveWatched()) {
            watched.erase(name);
            return r->send(507, "text/plain", "Too many watched jobs to store");
        }
        r->send(204);
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        {
            Lock g(mtx);
            doc["url"] = cfg.url;
            doc["user"] = cfg.user;
            doc["tokenSet"] = cfg.token.length() > 0;  // the token itself is never sent back
            doc["poll"] = cfg.pollSeconds;
        }
        sendJson(r, doc);
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (!fromUi(r)) return;
        String url = param(r, "url"), token = param(r, "token");
        url.trim();
        while (url.endsWith("/")) url.remove(url.length() - 1);
        if (!url.startsWith("http://")) return r->send(400, "text/plain", "URL must start with http://");
        {
            Lock g(mtx);
            if (url != cfg.url) cfg.token = "";  // never send the API token to a server it wasn't entered for
            if (token.length()) cfg.token = token;
            cfg.url = url;
            cfg.user = param(r, "user");
            cfg.pollSeconds = std::max(10L, param(r, "poll").toInt());
            prefs.putString("url", cfg.url);
            prefs.putString("user", cfg.user);
            prefs.putString("token", cfg.token);
            prefs.putUInt("poll", cfg.pollSeconds);
        }
        pollNow = true;
        r->send(204);
    });

    server.on("/api/light", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (!fromUi(r)) return;
        String mode = param(r, "mode");
        Lock g(mtx);
        manual = mode == "auto" ? "" : mode;
        r->send(204);
    });

    server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* r) {
        String s;
        {
            Lock g(logMtx);
            s = logText;
        }
        r->send(200, "text/plain", s);
    });

    server.begin();
}

void setup() {
    Serial.begin(115200);
    for (uint8_t pin : PINS) pinMode(pin, OUTPUT);
    serialVprintf = esp_log_set_vprintf(captureIdfLog);

    prefs.begin("buildstatus");
    cfg = {prefs.getString("url", "http://htkasrv084:8080"), prefs.getString("user"), prefs.getString("token"),
           prefs.getUInt("poll", 30)};
    String w = prefs.getString("watched");
    for (int i = 0, j; (j = w.indexOf('\n', i)) >= 0; i = j + 1) watched.insert(w.substring(i, j).c_str());

    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
        logf("WiFi connected: http://%s/ or http://buildstatus.local/", WiFi.localIP().toString().c_str());
        pollNow = true;
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // reconnects automatically
    MDNS.begin("buildstatus");
    setupWeb();
    logf("Started, Jenkins %s, %u watched jobs", cfg.url.c_str(), watched.size());
}
