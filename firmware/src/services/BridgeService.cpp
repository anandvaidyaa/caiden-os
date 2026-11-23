#include "BridgeService.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"
#include "../graphics/Color.h"
#include "../networking/NetworkManager.h"
#include <Crypto.h>
#include <Updater.h>

BridgeService g_bridge;

static const uint32_t STATE_MS = 600000;      // news, markets, quote, cricket: every 10 min
static const uint32_t CRICKET_LIVE_MS = 120000; // ... every 2 min while the followed team plays
static const uint32_t SP_ACTIVE_MS = 5000;    // Spotify while a player is active
static const uint32_t SP_IDLE_MS = 20000;     // ... and while nothing plays
static const uint32_t SP_OFF_MS = 300000;     // relay has no Spotify sign-in
static const uint32_t OFFLINE_MS = 90000;     // no relay answer this long = offline
static const char* ART_PATH = "/art.bin";

static uint16_t hex565(const char* s) {
    if (!s || s[0] != '#' || strlen(s) < 7) return 0;
    uint32_t v = strtoul(s + 1, nullptr, 16);
    return rgb(v);
}

static void copy(char* dst, size_t n, const char* src) { snprintf(dst, n, "%s", src ? src : ""); }

// RFC 3986 percent-encoding for the feed settings in the /state.json query
static void addEncoded(String& u, const char* s) {
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; s++) {
        const uint8_t ch = uint8_t(*s);
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') u += char(ch);
        else { u += '%'; u += hex[ch >> 4]; u += hex[ch & 15]; }
    }
}

static String relayUrl(const char* path) {
    String u = F("http://");
    u += g_config.get().relay;
    u += path;
    return u;
}

bool BridgeService::getJson(const String& path, uint16_t timeoutMs, JsonDocument& doc) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.useHTTP10(true);
    if (!http.begin(client, relayUrl(path.c_str()))) return false;
    const int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) { Serial.printf("[relay] json: %s\n", err.c_str()); return false; }
    return true;
}

void BridgeService::okNow() {
    BridgeData& b = g_state.bridge;
    if (!b.online) { b.version++; b.spVersion++; }
    b.online = b.relayOk = true;
    b.lastOkMs = millis();
    fails_ = 0;
}

void BridgeService::relayChanged() {
    stateNext_ = spNext_ = millis();
    fails_ = 0;
    hash_[0] = 0;
    g_state.bridge.relayOk = false;
}

// ---------------------------------------------------------------- news, markets, quote, cricket
bool BridgeService::fetchState() {
    const Config& c = g_config.get();
    String path = F("/state.json?f=");
    addEncoded(path, c.feeds);
    path += F("&m=");
    addEncoded(path, c.mkts);
    path += F("&b=");
    path += c.btc ? '1' : '0';
    path += F("&a=");
    path += c.newsAge;
    path += F("&team=");
    addEncoded(path, c.team);
    // Health report, kept by the relay at /clock.json so the clock can be checked without USB or
    // the (locked) settings page: version, uptime, free memory, signal, last restart, update error.
    path += F("&fw=" CAIDEN_FW_VERSION "&up=");
    path += millis() / 1000;
    path += F("&hp=");
    path += ESP.getFreeHeap();
    path += F("&ss=");
    path += g_state.net.rssi;
    path += F("&rr=");
    addEncoded(path, ESP.getResetReason().c_str());
    if (g_state.ota.err[0]) { path += F("&ue="); addEncoded(path, g_state.ota.err); }
    JsonDocument doc;
    if (!getJson(path, 8000, doc)) return false;   // the worker fetches ~10 sources: allow time
    applyState(doc);
    return true;
}

// An empty section never replaces data already shown (a source can fail for one round).
void BridgeService::applyState(JsonDocument& doc) {
    BridgeData& b = g_state.bridge;
    const time_t nowT = time(nullptr);
    const uint32_t at = nowT > 1600000000 ? uint32_t(nowT) : 0;
    // --- markets
    JsonArray mk = doc["mk"];
    if (mk.size()) {
        uint8_t n = 0;
        for (JsonObject r : mk) {
            if (n >= 5) break;
            MarketRow& m = b.mk[n++];
            copy(m.name, sizeof m.name, r["n"] | "");
            copy(m.value, sizeof m.value, r["v"] | "");
            copy(m.prev, sizeof m.prev, r["pv"] | "");
            m.change = r["c"] | 0.f;
            m.delta = r["d"] | 0.f;
            m.prevPos = r["pc"] | -1;
            JsonArray s = r["s"];
            m.sparkN = 0;
            for (JsonVariant v : s) { if (m.sparkN >= 24) break; m.spark[m.sparkN++] = uint8_t(v.as<int>()); }
        }
        b.markets = n;
        b.mkAt = at;
    }
    // --- news
    JsonArray nw = doc["nw"];
    if (nw.size()) {
        uint8_t n = 0;
        for (JsonObject it : nw) {
            if (n >= 8) break;
            NewsItem& e = b.nw[n++];
            copy(e.cat, sizeof e.cat, it["c"] | "");
            copy(e.head, sizeof e.head, it["h"] | "");
            copy(e.src, sizeof e.src, it["s"] | "");
            e.ageMin = it["a"] | -1;
        }
        b.news = n;
        b.nwAt = at;
    }
    // --- cricket (live only: always replaced)
    JsonObject cr = doc["cr"];
    CricketData& k = b.cr;
    k.live = cr["on"] | false;
    if (k.live) {
        copy(k.type, sizeof k.type, cr["mt"] | "");
        k.innings = cr["in"] | 0;
        copy(k.t1, sizeof k.t1, cr["t1"] | "");
        copy(k.t2, sizeof k.t2, cr["t2"] | "");
        copy(k.status, sizeof k.status, cr["st"] | "");
        k.target = cr["tg"] | 0;
        k.need = cr["nd"] | 0;
        k.balls = cr["bl"] | 0;
        uint8_t i = 0;
        for (JsonObject s : cr["sc"].as<JsonArray>()) {
            if (i >= 2) break;
            Innings& in = k.inn[i++];
            copy(in.team, sizeof in.team, s["t"] | "");
            in.runs = s["r"] | 0; in.wkts = s["w"] | 0; in.overs = s["o"] | 0.f;
        }
        for (; i < 2; i++) k.inn[i] = Innings();
    }
    // --- quote of the day
    const char* q = doc["qt"]["q"] | "";
    if (q[0]) {
        copy(b.qt.text, sizeof b.qt.text, q);
        copy(b.qt.author, sizeof b.qt.author, doc["qt"]["a"] | "");
        b.qtAt = at;
    }
    const char* h = doc["h"] | "";
    if (strcmp(h, hash_) != 0) { copy(hash_, sizeof hash_, h); b.version++; }   // redraw only on change
    Serial.printf("[relay] %u markets, %u headlines%s\n", b.markets, b.news, k.live ? ", live cricket" : "");
}

// ---------------------------------------------------------------- Spotify
bool BridgeService::fetchSpotify() {
    JsonDocument doc;
    if (!getJson(F("/sp.json"), 2500, doc)) return false;
    BridgeData& b = g_state.bridge;
    SpotifyData& sp = b.sp;
    b.spSetup = !(doc["x"] | 0);
    const bool on = doc["on"] | false;
    const bool pl = doc["pl"] | false;
    const char* id = doc["id"] | "";
    const char* ti = doc["ti"] | "";
    const bool trackChanged = on != sp.on || pl != sp.playing || strcmp(id, sp.artId) != 0 || strcmp(ti, sp.title) != 0;
    sp.on = on; sp.playing = pl;
    copy(sp.title, sizeof sp.title, ti);
    copy(sp.artist, sizeof sp.artist, doc["ar"] | "");
    sp.progressMs = doc["p"] | 0;
    sp.durationMs = doc["d"] | 0;
    sp.atMs = millis();
    copy(sp.artId, sizeof sp.artId, id);
    if (on && !sp.c1 && !sp.c2) { sp.c1 = hex565("#8A6BFF"); sp.c2 = hex565("#3FE0FF"); }   // until art colours arrive
    if (trackChanged) b.spVersion++;
    return true;
}

// Album art: 120x120 RGB565 from the relay, streamed to flash; the dominant colours come as headers.
bool BridgeService::fetchArt(const char* id) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(6000);
    String path = F("/art.bin?id=");
    path += id;
    if (!http.begin(client, relayUrl(path.c_str()))) return false;
    static const char* KEYS[] = {"X-C1", "X-C2"};
    http.collectHeaders(KEYS, 2);
    const int code = http.GET();
    const int len = http.getSize();
    if (code != HTTP_CODE_OK || len != ART * ART * 2) { http.end(); return false; }
    char c1[8], c2[8];
    copy(c1, sizeof c1, http.header("X-C1").c_str());
    copy(c2, sizeof c2, http.header("X-C2").c_str());
    File f = LittleFS.open("/art.tmp", "w");
    if (!f) { http.end(); return false; }
    WiFiClient* s = http.getStreamPtr();
    uint8_t buf[512];
    int got = 0;
    const uint32_t t0 = millis();
    while (got < len && millis() - t0 < 8000) {
        const size_t avail = s->available();
        if (!avail) { if (!s->connected()) break; delay(1); continue; }
        const int k = s->readBytes(buf, avail > sizeof buf ? sizeof buf : avail);
        if (k <= 0) break;
        f.write(buf, k);
        got += k;
        yield();
    }
    f.close();
    http.end();
    if (got != len) { LittleFS.remove("/art.tmp"); return false; }
    closeArt();
    if (!LittleFS.rename("/art.tmp", ART_PATH)) { LittleFS.remove("/art.tmp"); return false; }
    SpotifyData& sp = g_state.bridge.sp;
    if (hex565(c1) || hex565(c2)) { sp.c1 = hex565(c1); sp.c2 = hex565(c2); }
    copy(sp.artReady, sizeof sp.artReady, id);
    g_state.bridge.spVersion++;
    Serial.printf("[relay] artwork stored (%d B)\n", got);
    return true;
}

static File s_artFile;
static char s_artOpenId[44] = "";

void BridgeService::closeArt() {
    if (s_artFile) s_artFile.close();
    s_artOpenId[0] = 0;
}

bool BridgeService::artRow(int16_t row, uint16_t* out, int16_t x0, int16_t w) {
    File& f = s_artFile;
    const SpotifyData& sp = g_state.bridge.sp;
    if (!sp.artReady[0]) return false;
    if (!f || strcmp(s_artOpenId, sp.artReady) != 0) {
        if (f) f.close();
        f = LittleFS.open(ART_PATH, "r");
        if (!f) return false;
        copy(s_artOpenId, sizeof s_artOpenId, sp.artReady);
    }
    if (row < 0 || row >= ART || x0 < 0 || x0 + w > ART) return false;
    if (!f.seek(uint32_t(row) * ART * 2 + uint32_t(x0) * 2)) return false;
    return size_t(f.read(reinterpret_cast<uint8_t*>(out), size_t(w) * 2)) == size_t(w) * 2;   // little-endian = native
}

// ---------------------------------------------------------------- cloud firmware update
// The image travels over plain HTTP, so the clock trusts nothing the relay says by itself:
// /fw.json carries mac = HMAC-SHA256(update key, "caiden-fw:<version>:<size>:<md5>"), made on the PC
// with the same key the clock keeps in LittleFS (tools/ota_key.txt). The updater then refuses to
// commit unless the downloaded bytes match that MD5, so a changed image or a failed download leaves
// the running firmware in place. The version must be newer than this one (no replayed old image).
static const uint32_t FW_CHECK_MS = 6UL * 3600 * 1000;

static uint32_t verNum(const char* v) {            // "0.7.12" -> 0x00070C (8 bits each)
    uint32_t n = 0;
    for (int part = 0; part < 3; part++) {
        n = (n << 8) | (uint32_t(atoi(v)) & 0xFF);
        v = strchr(v, '.');
        if (!v) { n <<= 8 * (2 - part); break; }
        v++;
    }
    return n;
}

bool BridgeService::fetchFwMeta() {
    BridgeData& b = g_state.bridge;
    JsonDocument doc;
    if (!getJson(F("/fw.json"), 5000, doc)) return false;
    const char* v = doc["v"] | "";
    const uint32_t size = doc["size"] | 0;
    const char* md5 = doc["md5"] | "";
    const char* mac = doc["mac"] | "";
    b.fwV[0] = 0;
    bool ok = v[0] && strlen(v) < sizeof b.fwV && size > 4096 && strlen(md5) == 32 && strlen(mac) == 64;
    if (ok && verNum(v) <= verNum(CAIDEN_FW_VERSION)) ok = false;           // not newer: nothing to offer
    if (ok) {
        String msg = F("caiden-fw:");
        msg += v; msg += ':'; msg += size; msg += ':'; msg += md5;
        const String want = experimental::crypto::SHA256::hmac(msg, g_net.otaKey(), 32, 32);
        uint8_t diff = want.length() == 64 ? 0 : 1;
        for (size_t i = 0; i < 64 && !diff; i++) diff |= uint8_t(tolower(mac[i])) ^ uint8_t(tolower(want[i]));
        if (diff) { Serial.printf("[fw] relay offers %s but it is not signed for this clock\n", v); ok = false; }
    }
    if (ok) {
        copy(b.fwV, sizeof b.fwV, v);
        copy(b.fwMd5, sizeof b.fwMd5, md5);
        b.fwSize = size;
        Serial.printf("[fw] update %s available (%lu B)\n", v, (unsigned long)size);
    }
    return true;
}

bool BridgeService::requestInstall() {
    if (!g_state.bridge.fwV[0] || g_state.ota.phase == OtaPhase::Receiving) return false;
    installReq_ = true;
    return true;
}

static void fwFail(const char* why) {
    OtaState& o = g_state.ota;
    snprintf(o.err, sizeof o.err, "%s", why);
    o.phase = OtaPhase::Failed;
    o.failedAt = millis();
    Serial.printf("[fw] failed: %s (updater error %u)\n", why, unsigned(Update.getError()));
    if (g_net.onOtaProgress) g_net.onOtaProgress();
}

// One HTTP header line, without the line end. Keeps the first cap-1 characters and drops the rest,
// so a long header cannot be mistaken for the blank line. Returns the full length, or -1 on timeout.
static int headerLine(WiFiClient& c, char* out, size_t cap) {
    size_t n = 0, len = 0;
    const uint32_t t0 = millis();
    while (millis() - t0 < 8000) {
        const int ch = c.read();
        if (ch < 0) { if (!c.connected()) return -1; delay(1); continue; }
        if (ch == '\n') { out[n] = 0; return int(len); }
        if (ch == '\r') continue;
        len++;
        if (n < cap - 1) out[n++] = char(ch);
    }
    return -1;
}

void BridgeService::installUpdate() {
    BridgeData& b = g_state.bridge;
    OtaState& o = g_state.ota;
    // re-check right before; one failed request (seen once) should not cancel the install
    bool metaOk = false;
    for (uint8_t i = 0; i < 3 && !metaOk; i++) { if (i) delay(1000); metaOk = fetchFwMeta(); }
    if (!metaOk || !b.fwV[0]) { fwFail("update not available"); return; }
    closeArt();
    Serial.printf("[fw] installing %s\n", b.fwV);
    o.phase = OtaPhase::Receiving; o.pct = 0; o.err[0] = 0;
    if (g_net.onOtaProgress) g_net.onOtaProgress();
    if (!Update.begin(b.fwSize)) { fwFail("no room for update"); return; }
    if (!Update.setMD5(b.fwMd5)) { Update.end(); fwFail("bad checksum"); return; }
    // Download in small pieces (?o=&n=) with plain requests on one kept-open connection. The updater
    // already holds 4 KB of the ~11 KB free. HTTPClient's own buffers, plus a new connection per
    // piece, left the Wi-Fi driver too little memory and it crashed. A piece cut short is asked for
    // again from the last byte written, on a new connection.
    static const uint32_t PIECE = 16384;   // data in flight is capped by the TCP window (2 KB), not by this
    const String host = g_config.get().relay;
    uint8_t buf[512];
    char line[48];
    uint32_t got = 0;
    uint8_t lastPct = 0, tries = 0;
    WiFiClient c;
    c.setTimeout(8000);
    while (got < b.fwSize && tries < 6) {
        const uint32_t want = (b.fwSize - got < PIECE) ? b.fwSize - got : PIECE;
        const uint32_t before = got;
        // let the Wi-Fi driver get its buffers back before asking for more data
        for (uint8_t w = 0; w < 100 && (ESP.getFreeHeap() < 4000 || ESP.getMaxFreeBlockSize() < 1600); w++) delay(20);
        if ((got / PIECE) % 4 == 0)
            Serial.printf("[fw] t %lu ms at %u heap %u maxblk %u conn %d\n", millis(), unsigned(got),
                          ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), int(c.connected()));
        bool keep = false;
        if (c.connected() || c.connect(host.c_str(), 80)) {
            c.printf("GET /fw.bin?v=%s&o=%u&n=%u HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n",
                     b.fwV, unsigned(got), unsigned(want), host.c_str());
            int code = 0;
            long len = -1;
            keep = true;
            if (headerLine(c, line, sizeof line) > 12 && !strncmp(line, "HTTP/1.", 7)) code = atoi(line + 9);
            for (;;) {
                const int n = headerLine(c, line, sizeof line);
                if (n <= 0) { if (n < 0) code = -1; break; }   // blank line ends the headers
                if (!strncasecmp(line, "Content-Length:", 15)) len = atol(line + 15);
                else if (!strncasecmp(line, "Connection: close", 17)) keep = false;
            }
            if (code == 200 && len == long(want)) {
                uint32_t left = want, lastData = millis();
                while (left && millis() - lastData < 8000) {
                    const size_t avail = c.available();
                    if (!avail) { if (!c.connected()) break; delay(1); continue; }
                    const size_t n = avail > sizeof buf ? sizeof buf : avail;
                    const int k = c.read(buf, n > left ? left : n);
                    if (k <= 0) break;
                    if (Update.write(buf, k) != size_t(k)) { c.stop(); Update.end(); fwFail("flash write"); return; }
                    got += k; left -= k;
                    lastData = millis();
                }
            } else {
                Serial.printf("[fw] piece at %u: HTTP %d, %ld B\n", unsigned(got), code, len);
            }
            if (got - before != want) keep = false;
        }
        if (!keep) c.stop();
        if (got == before) { tries++; delay(500 * tries); } else tries = 0;
        const uint8_t pct = uint8_t(uint64_t(got) * 100 / b.fwSize);
        if (pct >= lastPct + 5) {
            lastPct = pct; o.pct = pct;
            if (g_net.onOtaProgress) g_net.onOtaProgress();
        }
        yield();
    }
    c.stop();
    // end() commits only when every byte arrived and the MD5 matches; otherwise nothing changes
    if (got != b.fwSize) {
        Serial.printf("[fw] stopped at %u of %u B\n", unsigned(got), unsigned(b.fwSize));
        Update.end(); fwFail("download interrupted");
        return;
    }
    if (!Update.end()) { fwFail("checksum mismatch"); return; }
    o.phase = OtaPhase::Restarting; o.pct = 100;
    if (g_net.onOtaProgress) g_net.onOtaProgress();
    Serial.println(F("[fw] image verified, restarting"));
    delay(1500);
    ESP.restart();
}

// ---------------------------------------------------------------- scheduler: one request per pass
void BridgeService::loop(bool online) {
    BridgeData& b = g_state.bridge;
    const uint32_t now = millis();
    if (b.online && now - b.lastOkMs > OFFLINE_MS) {   // relay went away: pages keep their last data
        b.online = b.relayOk = false;
        b.version++; b.spVersion++;
    }
    const Config& c = g_config.get();
    if (!online || !c.relay[0]) return;
    if (ESP.getMaxFreeBlockSize() < 6000) return;      // a portal request is holding memory: next pass
    auto retry = [&]() -> uint32_t {
        if (fails_ < 20) fails_++;
        const uint32_t wait = 15000UL * fails_;
        return millis() + (wait > 300000UL ? 300000UL : wait);
    };
    if (installReq_) { installReq_ = false; installUpdate(); return; }   // only after the user pressed Install
    if (int32_t(now - fwNext_) >= 0) {
        fwNext_ = millis() + (fetchFwMeta() ? FW_CHECK_MS : 600000UL);
        return;
    }
    if (int32_t(now - stateNext_) >= 0) {
        if (fetchState()) { okNow(); stateNext_ = millis() + (b.cr.live ? CRICKET_LIVE_MS : STATE_MS); }
        else stateNext_ = retry();
        return;
    }
    if (int32_t(now - spNext_) >= 0) {
        if (fetchSpotify()) { okNow(); spNext_ = millis() + (!b.spSetup ? SP_OFF_MS : b.sp.on ? SP_ACTIVE_MS : SP_IDLE_MS); }
        else spNext_ = retry();
        return;
    }
    const SpotifyData& sp = b.sp;
    if (sp.on && sp.artId[0] && strcmp(sp.artId, sp.artReady) != 0 && int32_t(now - artRetryAt_) >= 0) {
        if (strcmp(sp.artId, artFailId_) != 0) { artFails_ = 0; artFailId_[0] = 0; }
        if (artFails_ < 3) {
            if (fetchArt(sp.artId)) { artFails_ = 0; artFailId_[0] = 0; }
            else { copy(artFailId_, sizeof artFailId_, sp.artId); artFails_++; artRetryAt_ = millis() + 30000; }
            return;
        }
    }
}
