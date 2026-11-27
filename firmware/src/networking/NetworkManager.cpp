#include "NetworkManager.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Updater.h>
#include <Ticker.h>
#include <Crypto.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include "PortalPage.h"
#include "../services/ClockService.h"
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"
#include "../services/BridgeService.h"
#include "../services/FocusService.h"

NetworkManager g_net;
static void otaWatchStop();
bool requestPage(const char* name, const char* trans);   // main.cpp
bool applySetting(const char* key, const char* val, bool apply);   // main.cpp
uint16_t portalAvailableMask();                          // main.cpp
const char* portalCurrentPage();                         // main.cpp
static ESP8266WebServer server(80);
static DNSServer dns;
static const uint32_t CONNECT_TIMEOUT_MS = 45000;   // weak links (-80 dBm) need longer
static const uint32_t OFFLINE_TO_AP_MS = 5UL * 60 * 1000;
static const uint32_t STA_RETRY_EVERY_MS = 60000, STA_TRY_WINDOW_MS = 30000;

// --- diagnostics (setup troubleshooting): station joins/leaves and HTTP requests
static WiFiEventHandler evJoin, evLeave;
static void setupDiagnostics() {
    evJoin = WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected& e) {
        Serial.printf("[ap] phone joined (aid %u)\n", e.aid);
    });
    evLeave = WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected& e) {
        Serial.printf("[ap] phone left (aid %u)\n", e.aid);
    });
}

void NetworkManager::setMode(uint8_t m) {
    NetMode nm = NetMode(m);
    if (g_state.net.mode != nm) { g_state.net.mode = nm; g_state.net.version++; }
}

bool NetworkManager::online() const { return g_state.net.mode == NetMode::Online; }

// Random setup password (QA: the old chip-id-derived one was guessable from the BSSID).
static void ensureApPassword(Config& c) {
    if (strlen(c.apPass) >= 8) return;
    static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";   // no 0/o/1/l/i look-alikes
    for (int i = 0; i < 10; i++) c.apPass[i] = A[ESP.random() % (sizeof A - 1)];
    c.apPass[10] = 0;
    g_config.save();
}

void NetworkManager::begin() {
    Config& c = g_config.get();
    WiFi.persistent(false);              // credentials live in our config, not the SDK sector
    // Keep the radio awake: modem-sleep made the clock miss packets (66 % ping loss on a weak link),
    // which stalled bridge pushes. The clock is USB powered, so power saving buys nothing.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.hostname("caiden");
    ensureApPassword(c);
    webPassword();                       // settings-page password, shown on the System screen
    snprintf(g_state.net.apName, sizeof g_state.net.apName, "CAIDEN-%04X", ESP.getChipId() & 0xFFFF);
    snprintf(g_state.net.apPass, sizeof g_state.net.apPass, "%s", c.apPass);
    setupDiagnostics();
    setupRoutes();
    if (g_config.hasWifi()) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(c.ssid, c.pass);
        snprintf(g_state.net.ssid, sizeof g_state.net.ssid, "%s", c.ssid);
        connectStart_ = millis();
        setMode(uint8_t(NetMode::Connecting));
        Serial.printf("[net] connecting to %s\n", c.ssid);
    } else {
        startAP();
    }
}

void NetworkManager::cacheScan() {
    // Scan once before the AP comes up: scanning later makes the AP hop channels.
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    scanJson_ = "[";
    for (int i = 0, k = 0; i < n && k < 15; i++) {
        String s = WiFi.SSID(i);
        if (!s.length()) continue;
        if (k++) scanJson_ += ',';
        scanJson_ += F("{\"s\":\"");
        for (char ch : s) { if (ch == '"' || ch == '\\') scanJson_ += '\\'; if (uint8_t(ch) >= 0x20) scanJson_ += ch; }
        scanJson_ += F("\",\"r\":"); scanJson_ += WiFi.RSSI(i);
        scanJson_ += F(",\"l\":"); scanJson_ += WiFi.encryptionType(i) != ENC_TYPE_NONE ? 1 : 0;
        scanJson_ += '}';
    }
    scanJson_ += ']';
    WiFi.scanDelete();
}

void NetworkManager::startAP() {
    if (apActive_) return;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    cacheScan();
    WiFi.mode(WIFI_AP);                                          // AP only: stable channel
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(g_state.net.apName, g_state.net.apPass);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", IPAddress(192, 168, 4, 1));             // captive portal: every name -> us
    apActive_ = true;
    staTrying_ = false;
    nextStaTry_ = millis() + STA_RETRY_EVERY_MS;
    strcpy(g_state.net.ip, "192.168.4.1");
    setMode(uint8_t(NetMode::SetupAP));
    Serial.printf("[net] setup AP %s up\n", g_state.net.apName);
}

void NetworkManager::stopAP() {
    if (!apActive_) return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    apActive_ = false;
    staTrying_ = false;
}

static String esc(const char* s) {
    String o;
    for (; *s; s++) {
        char ch = *s;
        if (ch == '"') o += F("\\\""); else if (ch == '\\') o += F("\\\\");
        else if (uint8_t(ch) < 0x20) continue; else o += ch;
    }
    return o;
}

// Settings writes are allowed from the setup AP (WPA2, random password shown on the clock), or on
// the LAN only when the request carries our custom header (forces a CORS preflight that we never
// answer, so other web pages can't post here), a Host we recognise (blocks DNS rebinding) and,
// a login token. needLogin=false: routes with their own proof (the wireless update signs each
// upload with the clock's key) and the login routes.
static bool writeAllowed(bool apActive, bool needLogin = true) {
    if (apActive) return true;
    if (server.header("X-Caiden") != "1") return false;
    String host = server.hostHeader();
    int colon = host.indexOf(':');
    if (colon >= 0) host = host.substring(0, colon);
    host.toLowerCase();
    if (!(host == F("caiden") || host.startsWith(F("caiden.")) || host == WiFi.localIP().toString())) return false;
    return !needLogin || g_net.authed();
}

static void deny() {
    server.send(g_net.authed() ? 403 : 401, F("application/json"), F("{\"ok\":false}"));
}

// ---------------------------------------------------------------- settings-page password
// Made by the clock (10 characters, no look-alikes) and shown on its System screen. The login
// token is HMAC-SHA256(clock key, "caiden-session:" password): it survives restarts and stops
// working when a new password is made. Wrong passwords lock the login for 1 minute after 5 tries,
// doubling up to 16.
static void makeWebPassword(Config& c) {
    static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";
    for (int i = 0; i < 10; i++) c.webPass[i] = A[ESP.random() % (sizeof A - 1)];
    c.webPass[10] = 0;
    g_config.save();
}

static bool sameText(const String& a, const String& b) {   // compare without an early exit
    if (a.length() != b.length()) return false;
    uint8_t d = 0;
    for (size_t i = 0; i < a.length(); i++) d |= uint8_t(a[i]) ^ uint8_t(b[i]);
    return d == 0;
}

const char* NetworkManager::webPassword() {
    Config& c = g_config.get();
    if (strlen(c.webPass) < 8) makeWebPassword(c);
    return c.webPass;
}

String NetworkManager::sessionToken() {
    String m = F("caiden-session:");
    m += webPassword();
    return experimental::crypto::SHA256::hmac(m, otaKey(), 32, 32);
}

bool NetworkManager::authed() {
    const String t = server.header("X-Caiden-Auth");
    return t.length() == 64 && sameText(t, sessionToken());
}

bool NetworkManager::loginLocked(uint32_t now) { return lockUntil_ && int32_t(lockUntil_ - now) > 0; }

void NetworkManager::loginFailed(uint32_t now) {
    if (++loginFails_ >= 5) {
        const uint8_t extra = loginFails_ - 5 > 4 ? 4 : loginFails_ - 5;
        lockUntil_ = now + (60000UL << extra);                 // 1, 2, 4, 8, 16 minutes
        Serial.printf("[pw] %u wrong passwords: login locked for %lu s\n", loginFails_, (60000UL << extra) / 1000);
    }
}

// Truncate on a UTF-8 boundary so multi-byte city names never get cut mid-character.
static void putUtf8(char* dst, size_t n, const String& v) {
    size_t len = v.length();
    if (len > n - 1) {
        len = n - 1;
        while (len > 0 && (uint8_t(v[len]) & 0xC0) == 0x80) len--;
    }
    memcpy(dst, v.c_str(), len);
    dst[len] = 0;
}

static String htmlEsc(const String& s) {
    String o;
    o.reserve(s.length() + 8);
    for (char ch : s) {
        switch (ch) {
            case '&': o += F("&amp;"); break;
            case '<': o += F("&lt;"); break;
            case '>': o += F("&gt;"); break;
            case '"': o += F("&quot;"); break;
            case '\'': o += F("&#39;"); break;
            default: if (uint8_t(ch) >= 0x20) o += ch;
        }
    }
    return o;
}

// Setup network page. Built like the stock firmware's WiFiManager page, which worked on phones:
// small plain HTML, no gzip, no JavaScript, an ordinary form post. (The full portal is gzip +
// JavaScript and did not load on the phone in setup mode, 2026-11-22.)
static const char SETUP_HEAD[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'><title>CAIDEN setup</title><style>"
    "body{font-family:sans-serif;background:#0b0d12;color:#e9ecf2;margin:0;padding:16px}main{max-width:420px;margin:auto}"
    "h1{font-size:22px;margin:8px 0 4px}p{color:#9aa3b2;margin:0 0 16px}label{display:block;margin:14px 0 6px;font-weight:600}"
    "input,select,button{width:100%;box-sizing:border-box;font-size:16px;padding:12px;border-radius:10px;"
    "border:1px solid #2a3140;background:#141822;color:#e9ecf2}"
    ".n{display:flex;align-items:center;gap:10px;padding:10px 12px;border:1px solid #2a3140;border-radius:10px;margin:6px 0;font-weight:400}"
    ".n input{width:auto;margin:0}.n span{flex:1;word-break:break-all}.n small{color:#9aa3b2}"
    "button{background:#6c8cff;border:0;color:#fff;font-weight:700;margin-top:20px}.e{color:#ff9090}"
    "</style></head><body><main><h1>CAIDEN setup</h1>";

static const char* const SETUP_TZ[][2] = {
    {"IST-5:30", "India (IST)"}, {"UTC0", "UTC"}, {"GMT0BST,M3.5.0/1,M10.5.0", "London"},
    {"CET-1CEST,M3.5.0,M10.5.0/3", "Berlin / Paris"}, {"<+04>-4", "Dubai"}, {"<+08>-8", "Singapore"},
    {"JST-9", "Tokyo"}, {"AEST-10AEDT,M10.1.0,M4.1.0/3", "Sydney"}, {"EST5EDT,M3.2.0,M11.1.0", "New York"},
    {"PST8PDT,M3.2.0,M11.1.0", "Los Angeles"}};

String NetworkManager::setupPageHtml(const char* note) {
    Config& c = g_config.get();
    String h;
    h.reserve(4096);
    h = FPSTR(SETUP_HEAD);
    if (note) { h += F("<p class='e'>"); h += htmlEsc(note); h += F("</p>"); }
    else h += F("<p>Connect the clock to your Wi-Fi.</p>");
    h += F("<form method='post' action='/setup'><label>Wi-Fi network</label>");

    // networks seen when setup mode started (rescanning would move the setup network's channel)
    JsonDocument doc;
    JsonArray nets;
    if (!deserializeJson(doc, scanJson_)) nets = doc.as<JsonArray>();
    const size_t n = nets.isNull() ? 0 : nets.size();
    uint8_t order[16];
    size_t k = 0;
    for (size_t i = 0; i < n && k < 16; i++) order[k++] = uint8_t(i);
    for (size_t i = 1; i < k; i++)                                     // strongest first
        for (size_t j = i; j > 0 && int(nets[order[j]]["r"] | -100) > int(nets[order[j - 1]]["r"] | -100); j--) {
            uint8_t t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    bool checked = false;
    for (size_t i = 0; i < k; i++) {
        JsonObject o = nets[order[i]];
        const String s = o["s"] | "";
        const int r = o["r"] | -100;
        const bool sel = !checked && (s == c.ssid || i == 0);
        h += F("<label class='n'><input type='radio' name='ssid' value='"); h += htmlEsc(s); h += '\'';
        if (sel) { h += F(" checked"); checked = true; }
        h += F("><span>"); h += htmlEsc(s); h += F("</span><small>");
        h += r >= -60 ? F("strong") : r >= -75 ? F("good") : F("weak");
        if (int(o["l"] | 0)) h += F(" &middot; locked");
        h += F("</small></label>");
    }
    h += F("<label class='n'><input type='radio' name='ssid' value=''");
    if (!checked) h += F(" checked");
    h += F("><span>Other network (type below)</span></label><input name='other' placeholder='Network name' autocapitalize='none'>");
    h += F("<label>Wi-Fi password</label><input type='password' name='pass' autocomplete='off' placeholder='Leave empty for an open network'>");
    h += F("<label>City</label><input name='city' value='"); h += htmlEsc(c.city); h += F("'>");
    h += F("<label>Time zone</label><select name='tz'>");
    bool known = false;
    for (auto& z : SETUP_TZ) if (!strcmp(z[0], c.tz)) known = true;
    if (!known && c.tz[0]) { h += F("<option value='"); h += htmlEsc(c.tz); h += F("' selected>"); h += htmlEsc(c.tz); h += F("</option>"); }
    for (auto& z : SETUP_TZ) {
        h += F("<option value='"); h += htmlEsc(z[0]); h += '\'';
        if (!strcmp(z[0], c.tz)) h += F(" selected");
        h += '>'; h += z[1]; h += F("</option>");
    }
    h += F("</select><button type='submit'>Save and connect</button></form></main></body></html>");
    Serial.printf("[http] setup page %u bytes, %u networks\n", unsigned(h.length()), unsigned(k));
    return h;
}

String NetworkManager::setupSavedHtml(const String& ssid) {
    String h;
    h.reserve(1400);
    h = FPSTR(SETUP_HEAD);
    h += F("<p>Saved. The clock restarts and joins <b>"); h += htmlEsc(ssid);
    h += F("</b>.</p><p>Switch your phone back to your home Wi-Fi. If the clock cannot join, this setup network "
           "comes back in about a minute so you can try again.</p></main></body></html>");
    return h;
}

void NetworkManager::sendSetupPage(const char* note) {
    const String h = setupPageHtml(note);
    server.keepAlive(false);
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(200, F("text/html; charset=utf-8"), h);
}

// ---------------------------------------------------------------- setup network web server
// While the setup network is up, port 80 is served here instead of by ESP8266WebServer.
// ESP8266WebServer handles one connection at a time and closes idle ones within seconds.
// Chrome on Android opens spare connections first and sends the request on one of them later;
// it found them closed, retried, and gave up with ERR_TOO_MANY_RETRIES (2026-11-22; firmware
// 0.3.3 to 0.3.5 logged one GET per try and nothing more). This server keeps up to 4 connections
// open for 30 s each, answers with "Connection: close", waits until the phone has the bytes, and
// logs every connection. Form fields are never logged (the Wi-Fi password is one of them).
namespace {
struct ApConn {
    WiFiClient c;
    String req;
    uint32_t since = 0;
    uint16_t id = 0;
    bool used = false;
};
ApConn apConns[4];
uint16_t apConnSeq = 0;
const size_t AP_REQ_MAX = 2048;
const uint32_t AP_IDLE_MS = 30000;

int hexVal(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// One field of an application/x-www-form-urlencoded body, decoded.
String formArg(const String& body, const char* key) {
    const int kl = strlen(key);
    int i = 0;
    while (i < int(body.length())) {
        int amp = body.indexOf('&', i);
        if (amp < 0) amp = body.length();
        if (amp - i > kl && body[i + kl] == '=' && !strncmp(body.c_str() + i, key, kl)) {
            String v;
            for (int j = i + kl + 1; j < amp; j++) {
                char ch = body[j];
                if (ch == '+') ch = ' ';
                else if (ch == '%' && j + 2 < amp && hexVal(body[j + 1]) >= 0 && hexVal(body[j + 2]) >= 0) {
                    ch = char(hexVal(body[j + 1]) * 16 + hexVal(body[j + 2]));
                    j += 2;
                }
                v += ch;
            }
            return v;
        }
        i = amp + 1;
    }
    return String();
}

void apClose(ApConn& a, const __FlashStringHelper* why) {
    Serial.printf("[ap-http] #%u %s\n", a.id, String(why).c_str());
    a.c.stop();
    a.c = WiFiClient();
    a.req = String();
    a.used = false;
}
}  // namespace

void NetworkManager::apHttpLoop() {
    WiFiServer& srv = server.getServer();
    const uint32_t now = millis();
    while (srv.hasClient()) {
        ApConn* slot = nullptr;
        for (auto& a : apConns) if (!a.used) { slot = &a; break; }
        if (!slot) {                                   // all in use: free the oldest silent one
            for (auto& a : apConns)
                if (!a.req.length() && (!slot || int32_t(a.since - slot->since) < 0)) slot = &a;
            if (!slot) {
                WiFiClient c = srv.accept();
                c.stop();
                Serial.println(F("[ap-http] all connections busy, refused one"));
                break;
            }
            apClose(*slot, F("closed (silent, room for a new one)"));
        }
        slot->c = srv.accept();
        slot->c.setNoDelay(true);
        slot->req = String();
        slot->since = now;
        slot->id = ++apConnSeq;
        slot->used = true;
        Serial.printf("[ap-http] #%u open from %s\n", slot->id, slot->c.remoteIP().toString().c_str());
    }

    for (auto& a : apConns) {
        if (!a.used) continue;
        while (a.c.available() && a.req.length() < AP_REQ_MAX) {
            uint8_t b[128];
            const int n = a.c.read(b, sizeof b);
            if (n <= 0) break;
            for (int i = 0; i < n; i++) a.req += char(b[i]);
        }
        const int hdrEnd = a.req.indexOf(F("\r\n\r\n"));
        if (hdrEnd < 0) {
            if (a.req.length() >= AP_REQ_MAX) apClose(a, F("closed (request too long)"));
            else if (!a.c.connected() && !a.c.available())
                apClose(a, a.req.length() ? F("closed by phone mid-request") : F("closed by phone, unused"));
            else if (now - a.since > AP_IDLE_MS) apClose(a, F("closed (idle 30 s)"));
            continue;
        }
        const bool post = a.req.startsWith(F("POST "));
        size_t need = 0;
        if (post) {
            int cl = a.req.indexOf(F("Content-Length:"));
            if (cl < 0) cl = a.req.indexOf(F("content-length:"));
            if (cl >= 0 && cl < hdrEnd) need = size_t(atoi(a.req.c_str() + cl + 15));
            if (need > AP_REQ_MAX) { apClose(a, F("closed (form too long)")); continue; }
        }
        if (a.req.length() < size_t(hdrEnd) + 4 + need) {           // form body still arriving
            if (!a.c.connected() && !a.c.available()) apClose(a, F("closed by phone mid-form"));
            else if (now - a.since > AP_IDLE_MS) apClose(a, F("closed (idle 30 s)"));
            continue;
        }

        const int sp1 = a.req.indexOf(' ');
        const int sp2 = a.req.indexOf(' ', sp1 + 1);
        const String path = (sp1 > 0 && sp2 > sp1) ? a.req.substring(sp1 + 1, sp2) : String("/");
        int hp = a.req.indexOf(F("\r\nHost:"));
        if (hp < 0) hp = a.req.indexOf(F("\r\nhost:"));
        String host;
        if (hp >= 0 && hp < hdrEnd) { host = a.req.substring(hp + 7, a.req.indexOf('\r', hp + 2)); host.trim(); }
        Serial.printf("[ap-http] #%u %s %s host '%s'\n", a.id, post ? "POST" : "GET", path.c_str(), host.c_str());

        int code = 200;
        String page;
        if (path.startsWith(F("/favicon"))) {
            code = 404;
        } else if (post && path.startsWith(F("/setup"))) {
            const String body = a.req.substring(hdrEnd + 4, hdrEnd + 4 + need);
            String ssid = formArg(body, "other");
            if (!ssid.length()) ssid = formArg(body, "ssid");          // not trimmed: spaces are valid
            String city = formArg(body, "city");
            city.trim();
            if (!ssid.length()) page = setupPageHtml("Choose a network, or type its name under Other network.");
            else if (!applySetup(ssid.c_str(), formArg(body, "pass").c_str(), city.c_str(),
                                 formArg(body, "tz").c_str(), -1, ""))
                page = setupPageHtml("Could not save. Please try again.");
            else page = setupSavedHtml(ssid);
        } else {
            page = setupPageHtml(nullptr);
        }

        String head;
        head.reserve(160);
        head = code == 200 ? F("HTTP/1.1 200 OK\r\n") : F("HTTP/1.1 404 Not Found\r\n");
        head += F("Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
        head += page.length();
        head += F("\r\n\r\n");
        const uint32_t t0 = millis();
        size_t sent = a.c.write(head.c_str(), head.length());
        if (page.length()) sent += a.c.write(page.c_str(), page.length());
        const bool delivered = a.c.flush(3000);                   // until the phone acknowledged it
        Serial.printf("[ap-http] #%u answered %d, %u of %u bytes, %s in %u ms\n", a.id, code, unsigned(sent),
                      unsigned(head.length() + page.length()), delivered ? "delivered" : "NOT acknowledged",
                      unsigned(millis() - t0));
        apClose(a, F("done"));
    }
}

void NetworkManager::setupRoutes() {
    if (routes_) return;
    routes_ = true;
    static const char* HEADERS[] = {"X-Caiden", "X-Caiden-Auth", "X-Ota-Auth", "X-Ota-Size", "X-Ota-Md5"};
    server.collectHeaders(HEADERS, sizeof HEADERS / sizeof HEADERS[0]);
    server.addHook([](const String& method, const String& url, WiFiClient* client, ESP8266WebServer::ContentTypeFunction) {
        Serial.printf("[http] %s %s from %s\n", method.c_str(), url.c_str(), client->remoteIP().toString().c_str());
        return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
    });
    server.on("/", HTTP_GET, [this] {
        // Setup network: the page itself for any host name, never a redirect. Redirects to
        // http://192.168.4.1/ looped on the phone (ERR_TOO_MANY_REDIRECTS, 2026-11-22).
        if (apActive_) {
            Serial.printf("[http] / host '%s'\n", server.hostHeader().c_str());
            sendSetupPage(nullptr);
            return;
        }
        // The portal (portal/index.html, gzip, see tools/gen_portal.py) goes out from flash in small
        // RAM chunks with an explicit length; the outcome is logged.
        const size_t len = PORTAL_GZ_LEN;
        const uint32_t t0 = millis();
        server.sendHeader(F("Cache-Control"), F("no-store"));
        server.sendHeader(F("Content-Encoding"), F("gzip"));
        server.setContentLength(len);
        server.send(200, F("text/html; charset=utf-8"), "");
        WiFiClient client = server.client();
        char buf[512];
        size_t sent = 0;
        while (sent < len && client.connected()) {
            size_t n = len - sent > sizeof buf ? sizeof buf : len - sent;
            memcpy_P(buf, PORTAL_GZ + sent, n);
            size_t w = client.write(reinterpret_cast<const uint8_t*>(buf), n);
            if (w == 0) break;
            sent += w;
        }
        Serial.printf("[http] / sent %u/%u bytes in %lu ms (connected=%d)\n", unsigned(sent), unsigned(len),
                      (unsigned long)(millis() - t0), client.connected());
    });
    server.on("/api/page", HTTP_POST, [this] {             // navigation only: no settings change
        if (!writeAllowed(apActive_)) { deny(); return; }
        bool ok = requestPage(server.arg("name").c_str(), server.arg("t").c_str());
        server.send(ok ? 200 : 404, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
    });
    // Focus timers (no touch on this clock): /api/focus?a=pomo|pause|resume|reset|sw|swstop|swreset|cd&s=..|cdstop
    server.on("/api/focus", HTTP_POST, [this] {
        if (!writeAllowed(apActive_)) { deny(); return; }
        bool ok = g_focus.command(server.arg("a").c_str(), uint32_t(server.arg("s").toInt()));
        if (ok) requestPage("focus", "up");
        server.send(ok ? 200 : 400, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
    });
    // Wireless firmware update (home network only, never on the setup AP). Besides the usual write
    // checks, the request must carry the image size and MD5 and an HMAC-SHA256 over
    // "<nonce>:<size>:<md5>" made with this clock's update key (the key itself is never sent). The image
    // is staged in free flash; the bootloader copies it over the running firmware only after the
    // whole image arrived and its MD5 matched, so a failed or interrupted upload changes nothing.
    server.on("/api/update/nonce", HTTP_GET, [this] {     // one-time challenge, valid 60 s
        if (apActive_ || !writeAllowed(false, false)) { server.send(403, F("application/json"), F("{\"ok\":false}")); return; }
        static const char HEXD[] = "0123456789abcdef";
        for (int i = 0; i < 32; i++) otaNonce_[i] = HEXD[ESP.random() & 15];
        otaNonce_[32] = 0;
        otaNonceAt_ = millis();
        String r = F("{\"nonce\":\"");
        r += otaNonce_;
        r += F("\"}");
        server.send(200, F("application/json"), r);
    });
    server.on("/api/update", HTTP_POST, [this] {
        otaWatchStop();
        OtaState& o = g_state.ota;
        if (o.phase == OtaPhase::Restarting) {               // image verified; restart already scheduled
            server.send(200, F("application/json"), F("{\"ok\":true}"));
        } else {
            if (otaAuthed_ && o.phase == OtaPhase::Receiving) { Update.end(); otaFail("upload incomplete"); }
            String r = F("{\"ok\":false,\"err\":\"");
            r += otaErr_[0] ? otaErr_ : "no file";
            r += F("\"}");
            server.send(otaAuthed_ ? 400 : 403, F("application/json"), r);
        }
        otaAuthed_ = false;
    }, [this] { otaUpload(); });
    // Cloud update (image on the relay, checked by BridgeService): the user starts it here.
    server.on("/api/fwcheck", HTTP_POST, [this] {
        if (apActive_ || !writeAllowed(false)) { deny(); return; }
        g_bridge.checkUpdateNow();
        server.send(200, F("application/json"), F("{\"ok\":true}"));
    });
    server.on("/api/fwinstall", HTTP_POST, [this] {
        if (apActive_ || !writeAllowed(false)) { deny(); return; }
        const bool ok = g_bridge.requestInstall();
        server.send(ok ? 200 : 400, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
        server.client().flush(1500);   // the download blocks the loop next: get this reply out first
    });
    // Log in with the password from the clock's System screen; returns the token the page sends
    // as X-Caiden-Auth.
    server.on("/api/login", HTTP_POST, [this] {
        if (apActive_ || !writeAllowed(false, false)) { server.send(403, F("application/json"), F("{\"ok\":false}")); return; }
        const uint32_t now = millis();
        if (loginLocked(now)) {
            String r = F("{\"ok\":false,\"lock\":"); r += (lockUntil_ - now) / 1000 + 1; r += '}';
            server.send(429, F("application/json"), r);
            return;
        }
        String pw = server.arg("pw");
        pw.trim();
        pw.toLowerCase();                                    // the password has no capitals
        if (!sameText(pw, String(webPassword()))) {
            loginFailed(now);
            String r = F("{\"ok\":false,\"lock\":"); r += loginLocked(now) ? (lockUntil_ - now) / 1000 + 1 : 0; r += '}';
            server.send(401, F("application/json"), r);
            return;
        }
        loginFails_ = 0; lockUntil_ = 0;
        String r = F("{\"ok\":true,\"tok\":\""); r += sessionToken(); r += F("\"}");
        server.send(200, F("application/json"), r);
    });
    // "Show the password on the clock" from the lock screen: only switches the clock's screen to
    // System, so it is allowed without logging in.
    server.on("/api/showpw", HTTP_POST, [this] {
        if (apActive_ || !writeAllowed(false, false)) { server.send(403, F("application/json"), F("{\"ok\":false}")); return; }
        requestPage("system", "fade");
        server.send(200, F("application/json"), F("{\"ok\":true}"));
    });
    // New password: logs out every browser except this one (it gets the new token).
    server.on("/api/newpw", HTTP_POST, [this] {
        if (apActive_ || !writeAllowed(false)) { deny(); return; }
        makeWebPassword(g_config.get());
        requestPage("system", "fade");
        Serial.println(F("[pw] new settings-page password made"));
        String r = F("{\"ok\":true,\"tok\":\""); r += sessionToken(); r += F("\"}");
        server.send(200, F("application/json"), r);
    });
    // Mining analytics for the settings page (logged in): status, rates, shares, history, earnings.
    // The mining key is never sent back, only whether one is set.
    server.on("/api/mining", HTTP_GET, [this] {
        if (!apActive_ && !authed()) { deny(); return; }
        const Config& c = g_config.get();
        const MiningData& m = g_state.mine;
        static const char* const ST[] = {"off", "no account", "finding a pool", "connecting", "mining", "paused", "error"};
        String j; j.reserve(640);
        j = F("{\"on\":"); j += c.mineOn ? 1 : 0;
        j += F(",\"user\":\""); j += esc(c.ducoUser);
        j += F("\",\"rig\":\""); j += esc(c.ducoRig);
        j += F("\",\"key\":"); j += c.ducoKey[0] ? 1 : 0;
        j += F(",\"st\":\""); j += ST[uint8_t(m.status) < 7 ? uint8_t(m.status) : 6];
        j += F("\",\"why\":\""); j += esc(m.why);
        j += F("\",\"hr\":"); j += String(m.hr, 1);
        j += F(",\"acc\":"); j += m.acc;
        j += F(",\"rej\":"); j += m.rej;
        j += F(",\"diff\":"); j += m.diff;
        j += F(",\"secs\":"); j += m.mineMs / 1000;
        j += F(",\"pool\":\""); j += esc(m.poolName);
        j += F("\",\"relay\":"); j += m.relayOk ? 1 : 0;
        j += F(",\"bal\":"); j += String(m.bal, 4);
        j += F(",\"today\":"); j += String(m.today, 4);
        j += F(",\"inr\":"); j += String(m.inr, 8);
        j += F(",\"btc\":"); j += String(m.btcHr, 0);
        j += F(",\"phr\":"); j += String(m.poolHr, 1);
        j += F(",\"pacc\":"); j += m.poolAcc;
        j += F(",\"prej\":"); j += m.poolRej;
        j += F(",\"hist\":[");
        for (uint8_t i = 0; i < m.histN; i++) { if (i) j += ','; j += m.hist[i]; }
        j += F("]}");
        server.send(200, F("application/json"), j);
        server.client().flush(1500);
    });
    server.on("/ping", HTTP_GET, [] { server.send(200, F("text/plain"), F("CAIDEN ok")); });
    // OS connectivity checks (/generate_204, /hotspot-detect.html, /connecttest.txt, ...) have no
    // route on purpose: on the setup AP they fall through to onNotFound's 302 to the portal, the
    // same captive-portal behaviour as the stock firmware (WiFiManager). Android/iOS then show
    // "Sign in to network" and open the setup page in their own sign-in window. Answering "online"
    // (204) hid that prompt, and browsers sent their traffic over mobile data instead.
    server.on("/api/state", HTTP_GET, [this] {
        Config& c = g_config.get();
        if (!apActive_ && !authed()) {                       // not logged in: nothing else
            String r = F("{\"login\":true,\"theme\":\"");
            r += esc(c.theme);
            r += F("\",\"lock\":");
            r += loginLocked(millis()) ? (lockUntil_ - millis()) / 1000 + 1 : 0;
            r += '}';
            server.send(401, F("application/json"), r);
            return;
        }
        String j; j.reserve(768);
        j = F("{\"city\":\""); j += esc(c.city);
        j += F("\",\"tz\":\""); j += esc(c.tz);
        j += F("\",\"h24\":"); j += c.h24 ? F("true") : F("false");
        j += F(",\"theme\":\""); j += esc(c.theme);
        j += F("\",\"ssid\":\""); j += esc(c.ssid);
        j += F("\",\"rotate\":"); j += c.rotateSec;
        j += F(",\"story\":"); j += c.storySec;
        j += F(",\"hold\":"); j += c.holdMin;
        j += F(",\"rh\":\""); j += esc(c.relay); j += '"';
        j += F(",\"nfd\":\""); j += esc(c.feeds); j += '"';
        j += F(",\"mk\":\""); j += esc(c.mkts); j += '"';
        j += F(",\"btc\":"); j += c.btc ? 1 : 0;
        j += F(",\"nage\":"); j += c.newsAge;
        j += F(",\"team\":\""); j += esc(c.team); j += '"';
        j += F(",\"sps\":"); j += g_state.bridge.spSetup ? 1 : 0;
        j += F(",\"upd\":\""); j += g_state.bridge.fwV; j += '"';
        j += F(",\"mon\":"); j += c.mineOn ? 1 : 0;
        j += F(",\"nf\":"); j += c.nightFrom;
        j += F(",\"nt\":"); j += c.nightTo;
        j += F(",\"bright\":"); j += c.brightness;
        j += F(",\"glow\":"); j += c.glowPct;
        j += F(",\"pages\":"); j += c.pageMask;
        j += F(",\"av\":"); j += portalAvailableMask();
        j += F(",\"page\":\""); j += portalCurrentPage();
        j += F("\",\"ap\":"); j += apActive_ ? F("true") : F("false");
        j += F(",\"ip\":\""); j += g_state.net.ip;
        j += F("\",\"rssi\":"); j += g_state.net.rssi;
        j += F(",\"up\":"); j += millis() / 1000;
        j += F(",\"bridge\":"); j += g_state.bridge.online ? F("true") : F("false");
        j += F(",\"al\":\""); j += esc(g_state.alert.text); j += '"';
        struct tm t;
        if (g_clock.now(t)) { char hm[6]; snprintf(hm, sizeof hm, "%02d:%02d", t.tm_hour, t.tm_min); j += F(",\"time\":\""); j += hm; j += '"'; }
        // focus timers: phase (0 idle, 1 focus, 2 break), running, seconds left, session, stopwatch, countdown, minutes today
        j += F(",\"f\":{\"p\":"); j += uint8_t(g_focus.phase());
        j += F(",\"r\":"); j += g_focus.pomoRunning() ? 1 : 0;
        j += F(",\"l\":"); j += g_focus.pomoRemaining();
        j += F(",\"s\":"); j += g_focus.session();
        j += F(",\"sw\":"); j += g_focus.swElapsed();
        j += F(",\"swr\":"); j += g_focus.swRunning() ? 1 : 0;
        j += F(",\"cd\":"); j += g_focus.cdActive() ? g_focus.cdRemaining() : 0;
        j += F(",\"today\":"); j += g_focus.todayMinutes();
        j += F("},\"heap\":"); j += ESP.getFreeHeap();
        j += F(",\"fw\":\"" CAIDEN_FW_VERSION "\",\"md5\":\""); j += ESP.getSketchMD5();   // lets the updater confirm the new image runs
        j += F("\"}");
        server.send(200, F("application/json"), j);
        // The reply is larger than one TCP segment (536 B). Wait until the browser has acknowledged
        // it: when memory is short, closing the connection early turned into a reset that cut off
        // the end of the JSON.
        server.client().flush(1500);
    });
    // Live settings (theme, bright, glow, h24, rotate, pages, night): applied at once, no restart.
    server.on("/api/set", HTTP_POST, [this] {
        if (!writeAllowed(apActive_)) { deny(); return; }
        bool ok = false;
        for (int pass = 0; pass < 2; pass++) {            // validate every key first, then apply all
            for (int i = 0; i < server.args(); i++) {
                const String k = server.argName(i);
                if (k == F("plain")) continue;
                ok = applySetting(k.c_str(), server.arg(i).c_str(), pass == 1);
                if (!ok) break;
            }
            if (!ok) break;
        }
        if (ok) ok = g_config.save();
        server.send(ok ? 200 : 400, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
    });
    server.on("/api/restart", HTTP_POST, [this] {
        if (!writeAllowed(apActive_)) { deny(); return; }
        server.send(200, F("application/json"), F("{\"ok\":true}"));
        restartAt_ = millis() + 1000;
    });
    server.on("/api/scan", HTTP_POST, [this] {             // POST + write check: a scan blocks ~2 s
        if (!writeAllowed(apActive_)) { deny(); return; }
        if (apActive_) { server.send(200, F("application/json"), scanJson_.length() ? scanJson_ : String("[]")); return; }
        cacheScan();   // STA mode: scanning is safe
        WiFi.mode(WIFI_STA);
        server.send(200, F("application/json"), scanJson_);
    });
    server.on("/api/save", HTTP_POST, [this] {
        if (!writeAllowed(apActive_)) { deny(); return; }
        Config& c = g_config.get();
        String oldCity = c.city;
        if (server.hasArg("ssid") && server.arg("ssid").length()) {
            String ssid = server.arg("ssid");                      // not trimmed: spaces are valid
            bool sameNet = ssid == c.ssid;
            putUtf8(c.ssid, sizeof c.ssid, ssid);
            String pass = server.arg("pass");
            if (pass.length() || !sameNet) putUtf8(c.pass, sizeof c.pass, pass);   // empty + new SSID = open network
        }
        auto put = [](const char* key, char* dst, size_t n) {
            if (!server.hasArg(key)) return;
            String v = server.arg(key); v.trim();
            putUtf8(dst, n, v);
        };
        put("city", c.city, sizeof c.city);
        if (server.arg("tz").length()) put("tz", c.tz, sizeof c.tz);   // empty = keep
        put("theme", c.theme, sizeof c.theme);
        if (server.hasArg("h24")) c.h24 = server.arg("h24") == "1";
        if (server.hasArg("night")) {
            String n = server.arg("night");
            int dash = n.indexOf('-');
            if (dash > 0) {
                long f = n.substring(0, dash).toInt(), to = n.substring(dash + 1).toInt();
                if (f >= 0 && f < 24 && to >= 0 && to < 24) { c.nightFrom = uint8_t(f); c.nightTo = uint8_t(to); }
            }
        }
        if (server.hasArg("rotate")) { long r = server.arg("rotate").toInt(); if (r >= 0 && r <= 600) c.rotateSec = uint16_t(r); }
        if (oldCity != c.city) { c.geoResolved = false; c.geoFailed = false; }
        bool ok = g_config.save();
        server.send(ok ? 200 : 500, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
        if (ok) restartAt_ = millis() + 1500;    // apply cleanly via restart
    });
    // Setup network form (plain HTML page above).
    server.on("/setup", HTTP_POST, [this] {
        if (!apActive_) { server.send(404, F("text/plain"), F("not found")); return; }
        String ssid = server.arg("other");
        if (!ssid.length()) ssid = server.arg("ssid");                 // not trimmed: spaces are valid
        if (!ssid.length()) { sendSetupPage("Choose a network, or type its name under Other network."); return; }
        String city = server.arg("city");
        city.trim();
        const String tz = server.arg("tz");
        if (!applySetup(ssid.c_str(), server.arg("pass").c_str(), city.c_str(), tz.c_str(), -1, "")) {
            sendSetupPage("Could not save. Please try again.");
            return;
        }
        const String h = setupSavedHtml(ssid);
        server.keepAlive(false);
        server.sendHeader(F("Cache-Control"), F("no-store"));
        server.send(200, F("text/html; charset=utf-8"), h);
    });
    server.onNotFound([this] {
        if (!apActive_) { server.send(404, F("text/plain"), F("not found")); return; }
        // Setup network: every other address (phone connectivity checks such as /generate_204 included)
        // gets the setup page with 200. Any answer other than 204 makes the phone offer "Sign in",
        // and with no redirect there is nothing to loop.
        Serial.printf("[http] %s host '%s' -> setup page\n", server.uri().c_str(), server.hostHeader().c_str());
        sendSetupPage(nullptr);
    });
    server.begin();
}

// ---------------------------------------------------------------- wireless update
// The web server waits for upload bytes with no timeout, so a sender that vanishes mid-upload
// (PC asleep, Wi-Fi lost) would freeze the clock. This timer restarts it after 20 s without data.
// Safe at any point: before Update.end() nothing is staged, after it the restart is wanted anyway.
static Ticker s_otaWatch;
static volatile uint32_t s_otaLastData = 0;
static void otaWatchTick() { if (millis() - s_otaLastData > 20000) system_restart(); }
static void otaWatchStart() { s_otaLastData = millis(); s_otaWatch.attach_ms(1000, otaWatchTick); }
static void otaWatchStop() { s_otaWatch.detach(); }

const char* NetworkManager::otaKey() {
    if (otaKey_[0]) return otaKey_;
    File f = LittleFS.open("/ota.key", "r");
    if (f) {
        size_t n = f.readBytes(otaKey_, 32);
        f.close();
        otaKey_[n] = 0;
        bool ok = n == 32;
        for (size_t i = 0; ok && i < n; i++) ok = isxdigit(uint8_t(otaKey_[i]));
        if (ok) return otaKey_;
    }
    static const char HEXD[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) otaKey_[i] = HEXD[ESP.random() & 15];   // hardware RNG, 128 bits
    otaKey_[32] = 0;
    f = LittleFS.open("/ota.key", "w");
    if (f) { f.write(reinterpret_cast<const uint8_t*>(otaKey_), 32); f.close(); }
    return otaKey_;
}

// Proof of the key without sending it: X-Ota-Auth = HMAC-SHA256(key, "<nonce>:<size>:<md5>"),
// using the nonce from /api/update/nonce (single use, 60 s). Bound to this exact image.
bool NetworkManager::otaAuthOk(uint32_t size, const String& md5) {
    const bool fresh = otaNonce_[0] && millis() - otaNonceAt_ <= 60000;
    String msg = otaNonce_;
    otaNonce_[0] = 0;                                    // single use, whatever the outcome
    if (!fresh) return false;
    msg += ':'; msg += size; msg += ':'; msg += md5;
    const String mac = experimental::crypto::SHA256::hmac(msg, otaKey(), 32, 32);
    const String got = server.header("X-Ota-Auth");
    if (got.length() != mac.length() || mac.length() != 64) return false;
    uint8_t diff = 0;                                    // constant-time compare
    for (size_t i = 0; i < 64; i++) diff |= uint8_t(tolower(got[i])) ^ uint8_t(tolower(mac[i]));
    return diff == 0;
}

void NetworkManager::otaReject(const char* why) {       // refused before anything was written
    snprintf(otaErr_, sizeof otaErr_, "%s", why);
    Serial.printf("[ota] rejected: %s\n", why);
}

void NetworkManager::otaFail(const char* why) {         // failed after it started: show it on screen
    OtaState& o = g_state.ota;
    snprintf(otaErr_, sizeof otaErr_, "%s", why);
    snprintf(o.err, sizeof o.err, "%s", why);
    o.phase = OtaPhase::Failed;
    o.failedAt = millis();
    Serial.printf("[ota] failed: %s (updater error %u)\n", why, unsigned(Update.getError()));
    if (onOtaProgress) onOtaProgress();
}

// While an upload streams in, the web server serves nobody else, but lwIP still accepts up to
// 5 new connections and buffers their data (bridge pushes, album art, portal polls). A few of those
// exhaust the heap mid-update (seen 2026-11-22: Exception 29 at ~64 s). So every queued connection
// gets a short "busy" reply and is closed at once, which frees its memory.
void NetworkManager::otaDrainClients() {
    WiFiServer& srv = server.getServer();
    while (srv.hasClient()) {
        WiFiClient c = srv.accept();
        c.print(F("HTTP/1.1 503 Service Unavailable\r\nRetry-After: 60\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
        c.stop();
    }
}

void NetworkManager::otaUpload() {
    HTTPUpload& up = server.upload();
    OtaState& o = g_state.ota;
    if (up.status == UPLOAD_FILE_START) {
        otaWatchStart();
        // a second file part must not disturb an update in progress
        if (o.phase == OtaPhase::Receiving || o.phase == OtaPhase::Restarting) { otaReject("busy"); return; }
        otaAuthed_ = false;
        otaErr_[0] = 0;
        if (apActive_ || !writeAllowed(false, false)) { otaReject("not allowed"); return; }
        const uint32_t size = strtoul(server.header("X-Ota-Size").c_str(), nullptr, 10);
        const String md5 = server.header("X-Ota-Md5");
        bool md5ok = md5.length() == 32;
        for (size_t i = 0; md5ok && i < 32; i++) md5ok = isdigit(uint8_t(md5[i])) || (md5[i] >= 'a' && md5[i] <= 'f');
        if (size < 4096 || !md5ok) { otaReject("bad size or md5"); return; }
        if (!otaAuthOk(size, md5)) { otaReject("not authorised"); return; }
        if (!Update.begin(size)) {
            snprintf(otaErr_, sizeof otaErr_, "cannot start (error %u)", unsigned(Update.getError()));
            Serial.printf("[ota] rejected: %s\n", otaErr_);
            return;
        }
        if (!Update.setMD5(md5.c_str())) { Update.end(); otaReject("bad md5"); return; }
        otaAuthed_ = true;
        otaLastPct_ = 0;
        o.phase = OtaPhase::Receiving;
        o.pct = 0;
        o.err[0] = 0;
        Serial.printf("[ota] receiving %lu bytes\n", (unsigned long)size);
        if (onOtaProgress) onOtaProgress();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        s_otaLastData = millis();
        if (!otaAuthed_ || o.phase != OtaPhase::Receiving) return;
        otaDrainClients();
        if (Update.write(up.buf, up.currentSize) != up.currentSize) { otaFail("flash write failed"); return; }
        const uint8_t pct = uint8_t(uint64_t(Update.progress()) * 100 / Update.size());
        if (pct >= otaLastPct_ + 5) {
            otaLastPct_ = pct;
            o.pct = pct;
            if (onOtaProgress) onOtaProgress();
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (!otaAuthed_ || o.phase != OtaPhase::Receiving) return;
        if (!Update.end()) { otaFail(Update.getError() == UPDATE_ERROR_MD5 ? "checksum mismatch" : "image incomplete"); return; }
        o.phase = OtaPhase::Restarting;
        o.pct = 100;
        restartAt_ = millis() + 1500;                    // even if the client vanishes before the reply
        Serial.println(F("[ota] image verified, restarting"));
        if (onOtaProgress) onOtaProgress();
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        otaWatchStop();
        if (otaAuthed_ && o.phase == OtaPhase::Receiving) { Update.end(); otaFail("upload aborted"); }
    }
}

bool NetworkManager::applySetup(const char* ssid, const char* pass, const char* city, const char* tz, int h24,
                                const char* theme) {
    Config& c = g_config.get();
    if (ssid && *ssid) {
        bool sameNet = strcmp(ssid, c.ssid) == 0;
        putUtf8(c.ssid, sizeof c.ssid, String(ssid));
        if ((pass && *pass) || !sameNet) putUtf8(c.pass, sizeof c.pass, String(pass ? pass : ""));
    }
    if (city && *city && strcmp(city, c.city) != 0) {
        putUtf8(c.city, sizeof c.city, String(city));
        c.geoResolved = false; c.geoFailed = false;
    }
    if (tz && *tz) putUtf8(c.tz, sizeof c.tz, String(tz));
    if (theme && *theme) putUtf8(c.theme, sizeof c.theme, String(theme));
    if (h24 == 0 || h24 == 1) c.h24 = h24 == 1;
    if (!g_config.save()) return false;
    restartAt_ = millis() + 800;
    return true;
}

void NetworkManager::goOnline() {
    if (apActive_) stopAP();
    offlineSince_ = 0;
    snprintf(g_state.net.ip, sizeof g_state.net.ip, "%s", WiFi.localIP().toString().c_str());
    g_state.net.rssi = WiFi.RSSI();
    static bool mdns = false;
    if (!mdns) { mdns = MDNS.begin("caiden"); if (mdns) MDNS.addService("http", "tcp", 80); }
    setMode(uint8_t(NetMode::Online));
    Serial.printf("[net] online %s rssi %d\n", g_state.net.ip, g_state.net.rssi);
}

void NetworkManager::loop() {
    if (apActive_) dns.processNextRequest();
    static uint32_t lastApLog = 0;
    if (apActive_ && millis() - lastApLog > 15000) {
        lastApLog = millis();
        Serial.printf("[ap] %s ip %s ch %d stations %d\n", g_state.net.apName, WiFi.softAPIP().toString().c_str(),
                      WiFi.channel(), WiFi.softAPgetStationNum());
    }
    if (apActive_) apHttpLoop();                 // setup network: own server (see apHttpLoop)
    else server.handleClient();
    if (g_state.net.mode == NetMode::Online) MDNS.update();

    uint32_t now = millis();
    if (restartAt_ && int32_t(now - restartAt_) >= 0) {
        Serial.println(F("[net] restarting to apply settings"));
        delay(50);
        ESP.restart();
    }

    bool up = WiFi.status() == WL_CONNECTED;
    switch (g_state.net.mode) {
        case NetMode::Connecting:
            if (up) { goOnline(); return; }
            if (now - connectStart_ > CONNECT_TIMEOUT_MS) {
                // 1 no SSID seen, 4 connect failed, 6 wrong password, 7 disconnected (core wl_status_t)
                Serial.printf("[net] connect timeout (status %d)\n", int(WiFi.status()));
                startAP();
            }
            return;

        case NetMode::SetupAP:
            if (!g_config.hasWifi()) return;
            if (staTrying_) {
                if (up) { goOnline(); return; }
                const bool phoneJoined = WiFi.softAPgetStationNum() > 0;
                if (phoneJoined || int32_t(now - staTryUntil_) >= 0) {   // back to a quiet AP
                    Serial.printf("[net] retry failed (status %d)\n", int(WiFi.status()));
                    WiFi.disconnect();
                    WiFi.mode(WIFI_AP);
                    staTrying_ = false;
                    nextStaTry_ = now + STA_RETRY_EVERY_MS;
                }
            } else if (int32_t(now - nextStaTry_) >= 0) {
                if (WiFi.softAPgetStationNum() == 0) {         // nobody on the portal: safe to retry
                    Config& c = g_config.get();
                    WiFi.mode(WIFI_AP_STA);
                    WiFi.begin(c.ssid, c.pass);
                    staTrying_ = true;
                    staTryUntil_ = now + STA_TRY_WINDOW_MS;
                } else {
                    nextStaTry_ = now + STA_RETRY_EVERY_MS;
                }
            }
            return;

        case NetMode::Online:
            if (!up) {
                setMode(uint8_t(NetMode::Offline));
                offlineSince_ = now;
                Serial.println(F("[net] link lost"));
            } else if (now - lastRssi_ > 5000) {
                lastRssi_ = now;
                int8_t r = WiFi.RSSI();
                if (abs(r - g_state.net.rssi) >= 3) { g_state.net.rssi = r; g_state.net.version++; }
            }
            return;

        case NetMode::Offline:
            if (up) { goOnline(); return; }
            if (now - offlineSince_ > OFFLINE_TO_AP_MS) { Serial.println(F("[net] offline 5 min -> setup AP")); startAP(); }
            return;

        default:
            return;
    }
}
