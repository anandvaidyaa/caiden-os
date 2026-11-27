// CAIDEN OS - firmware entry point.
// Cooperative main loop: network -> services -> UI. Nothing blocks for long; HTTP
// requests have short timeouts and run one per loop pass; rendering yields per band.
#include <Arduino.h>
#include <ArduinoJson.h>
#include "display/St7789.h"
#include "graphics/Renderer.h"
#include "ui/UIManager.h"
#include "screens/OrbitScreen.h"
#include "screens/IdleScreen.h"
#include "screens/EngineTestScreen.h"
#include "screens/Dashboards.h"
#include "screens/BridgeScreens.h"
#include "screens/MoreScreens.h"
#include "services/FocusService.h"
#include "screens/SystemScreens.h"
#include "screens/MiningScreen.h"
#include "services/MinerService.h"
#include "storage/StorageManager.h"
#include "configuration/ConfigurationManager.h"
#include "networking/NetworkManager.h"
#include "services/ClockService.h"
#include "services/WeatherService.h"
#include "services/BridgeService.h"
#include "services/LanScanService.h"
#include "state/AppState.h"

AppState g_state;

static St7789 lcd;
static Renderer renderer(lcd);
static BootScreen bootScreen;
static SetupScreen setupScreen;
static OrbitScreen homeScreen;     // CAIDEN OS 2.0 home: concept A "Orbit"
static IdleScreen idleScreen;
static EngineTestScreen engineScreen;
static WeatherScreen weatherScreen;
static AirScreen airScreen;
static SpotifyScreen spotifyScreen;
static MarketsScreen marketsScreen;
static NewsScreen newsScreen;
static CalendarScreen calendarScreen;
static ProductivityScreen focusScreen;
static WorldScreen worldScreen;
static CricketScreen cricketScreen;
static SystemScreen systemScreen;
static SunScreen sunScreen;
static QuoteScreen quoteScreen;
static LanScreen lanScreen;
static MiningScreen miningScreen;
static UpdateScreen updateScreen;

// Rotating dashboard pages (bridge-backed pages arrive with M5). Idle is not part of the rotation.
static Screen* const PAGES[] = {&homeScreen, &spotifyScreen, &focusScreen, &cricketScreen, &weatherScreen, &airScreen,
                                &calendarScreen, &worldScreen, &marketsScreen, &newsScreen,
                                &sunScreen, &quoteScreen, nullptr, &lanScreen, &miningScreen};   // bits 10-13 (portal PG order); bit 12 was
                                // Connection (removed in 0.8.5): the slot stays so saved page bits keep their meaning
// (the System page is diagnostics: open it with /api/page?name=system or the web page)
static const uint8_t PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);

static uint8_t brightness255() {
    uint8_t b = g_config.get().brightness;
    if (b > 100) b = 100;
    return b < 3 ? 8 : uint8_t(b * 255 / 100);
}

// ---------------------------------------------------------------- page control
static Screen* manualPage = nullptr;
static Transition manualTrans = Transition::PushUp;
static uint32_t manualUntil = 0;
// a page picked by hand wins for Config::holdMin minutes (portal: Pages tab)

static Screen* pageByName(const char* n) {
    if (!n) return nullptr;
    if (!strcmp(n, "home")) return &homeScreen;
    if (!strcmp(n, "idle")) return &idleScreen;
    if (!strcmp(n, "weather")) return &weatherScreen;
    if (!strcmp(n, "air")) return &airScreen;
    if (!strcmp(n, "spotify")) return &spotifyScreen;
    if (!strcmp(n, "markets")) return &marketsScreen;
    if (!strcmp(n, "news")) return &newsScreen;
    if (!strcmp(n, "calendar")) return &calendarScreen;
    if (!strcmp(n, "focus") || !strcmp(n, "productivity")) return &focusScreen;
    if (!strcmp(n, "world")) return &worldScreen;
    if (!strcmp(n, "cricket") || !strcmp(n, "sports")) return &cricketScreen;
    if (!strcmp(n, "system")) return &systemScreen;
    if (!strcmp(n, "sun") || !strcmp(n, "moon")) return &sunScreen;
    if (!strcmp(n, "quote")) return &quoteScreen;
    if (!strcmp(n, "lan") || !strcmp(n, "network")) return &lanScreen;
    if (!strcmp(n, "mining")) return &miningScreen;
    if (!strcmp(n, "engine")) return &engineScreen;     // M3.5 engine benchmark scene
    return nullptr;
}

static Transition transByName(const char* t) {
    if (t && !strcmp(t, "down")) return Transition::PushDown;
    if (t && !strcmp(t, "fade")) return Transition::Fade;
    if (t && !strcmp(t, "none")) return Transition::None;
    return Transition::PushUp;
}

// Called from serial and from the web API (/api/page).
bool requestPage(const char* name, const char* trans) {
    Screen* s = pageByName(name);
    if (!s) return false;
    manualPage = s;
    manualTrans = transByName(trans);
    manualUntil = millis() + uint32_t(g_config.get().holdMin) * 60000UL;
    return true;
}

static bool isNight() {
    struct tm t;
    if (!g_clock.now(t)) return false;
    const Config& c = g_config.get();
    if (c.nightFrom == c.nightTo) return false;
    return c.nightFrom > c.nightTo ? (t.tm_hour >= c.nightFrom || t.tm_hour < c.nightTo)
                                   : (t.tm_hour >= c.nightFrom && t.tm_hour < c.nightTo);
}

// Relay-fed pages join the rotation only while they have something to show. When the relay can't be
// reached they keep the last data they received (the header shows its age). Cricket is live-only.
// Home network (lan) needed the PC bridge and is gone; its slot stays so page bits don't move.
static bool pageAvailable(uint8_t i) {
    const BridgeData& b = g_state.bridge;
    Screen* s = PAGES[i];
    if (!s) return false;                // removed page
    if (i > 0 && !(g_config.get().pageMask & (1u << i))) return false;   // turned off in the portal
    // Spotify, Focus and Calendar always rotate (they show a "nothing now" screen when empty).
    // Cricket only while a match of the followed team (bridge cricket_team, default India) is live.
    if (s == &cricketScreen) return b.online && b.cr.live;
    if (s == &marketsScreen) return b.markets > 0;
    if (s == &newsScreen) return b.news > 0;
    if (s == &quoteScreen) return b.qt.text[0];
    if (s == &lanScreen) return b.ln.count > 0;
    if (s == &miningScreen) return g_config.get().mineOn;
    if (s == &sunScreen) return g_state.weather.valid;
    return true;
}

// Auto-rotation: home dwells 2x the interval, dashboards 1x; push-up between pages.
static uint8_t rotIdx = 0;
static uint32_t rotSince = 0;

static void choosePage(uint32_t now) {
    if (now < 1200 || g_ui.transitioning()) return;
    OtaState& ota = g_state.ota;                         // a wireless update owns the screen
    if (ota.phase == OtaPhase::Failed && now - ota.failedAt > 6000) ota.phase = OtaPhase::Idle;
    if (ota.phase != OtaPhase::Idle) { g_ui.show(&updateScreen, Transition::Fade, -1, 0, brightness255()); return; }
    Screen* want;
    Transition tr = Transition::PushUp;
    const uint16_t rot = g_config.get().rotateSec;
    if (g_state.net.mode == NetMode::SetupAP) {
        want = &setupScreen; tr = Transition::Fade;
    } else if (manualPage && int32_t(manualUntil - now) > 0) {
        want = manualPage; tr = manualTrans;
        rotSince = now;
    } else if (isNight()) {
        manualPage = nullptr;
        want = &idleScreen; tr = Transition::Fade;
    } else {
        manualPage = nullptr;
        Screen* cur = g_ui.target();
        bool inRotation = false;
        for (uint8_t i = 0; i < PAGE_COUNT; i++) if (PAGES[i] == cur) { rotIdx = i; inRotation = true; }
        if (!inRotation) { rotIdx = 0; rotSince = now; want = PAGES[0]; tr = Transition::Fade; }
        else if (rot > 0) {
            uint32_t dwell = uint32_t(rot) * 1000UL * (rotIdx == 0 ? 2 : 1);
            bool gone = !pageAvailable(rotIdx);                 // e.g. music stopped
            if (gone || int32_t(now - rotSince) >= int32_t(dwell)) {
                do { rotIdx = uint8_t((rotIdx + 1) % PAGE_COUNT); } while (!pageAvailable(rotIdx));
                rotSince = now;
            }
            want = PAGES[rotIdx];
        } else {
            want = PAGES[0];                     // rotation off: stay on home (manual pages still allowed)
            if (cur != want) tr = Transition::PushDown;
        }
    }
    if (want == g_ui.target()) return;
    // screensaver dims the backlight to 25 % (applied as the page appears, no jump)
    uint8_t b = brightness255();
    int16_t target = want == &idleScreen ? (b / 4 < 8 ? 8 : b / 4) : b;
    int8_t idx = -1;
    for (uint8_t i = 0; i < PAGE_COUNT; i++) if (PAGES[i] == want) idx = int8_t(i);
    g_ui.show(want, tr, idx, PAGE_COUNT, target);
}

// ---------------------------------------------------------------- web portal hooks
// Pages the rotation would show right now (bit = PAGES index), for the portal's page grid.
uint16_t portalAvailableMask() {
    uint16_t m = 0;
    for (uint8_t i = 0; i < PAGE_COUNT; i++) if (pageAvailable(i)) m |= uint16_t(1u << i);
    return m;
}
const char* portalCurrentPage() { return g_ui.target() ? g_ui.target()->name() : "-"; }

// Live settings from the portal (/api/set). With apply=false it only validates, so the caller
// can check every key before changing anything; the caller saves the config afterwards.
bool applySetting(const char* key, const char* val, bool apply) {
    Config& c = g_config.get();
    char* end = nullptr;
    const long v = strtol(val, &end, 10);
    const bool num = end && end != val && *end == 0;
    if (!strcmp(key, "theme")) {
        static const char* NAMES[] = {"midnight", "aurora", "crystal", "hypertech"};
        for (const char* n : NAMES) {
            if (strcmp(n, val)) continue;
            if (apply) { snprintf_P(c.theme, sizeof c.theme, PSTR("%s"), n); g_ui.setTheme(themeFromName(n)); }
            return true;
        }
        return false;
    }
    if (!strcmp(key, "bright")) {
        if (!num || v < 0 || v > 100) return false;
        if (apply) { c.brightness = uint8_t(v); g_ui.setBrightness(brightness255()); }
        return true;
    }
    if (!strcmp(key, "glow")) {
        if (!num || v < 0 || v > 200) return false;
        if (apply) { c.glowPct = g_glowPct = uint8_t(v); g_ui.invalidateAll(); }
        return true;
    }
    if (!strcmp(key, "h24")) {
        if (!num || (v != 0 && v != 1)) return false;
        if (apply) { c.h24 = v == 1; g_ui.invalidateAll(); }
        return true;
    }
    if (!strcmp(key, "rotate")) {
        if (!num || v < 0 || v > 600) return false;
        if (apply) { c.rotateSec = uint16_t(v); rotSince = millis(); }
        return true;
    }
    if (!strcmp(key, "pages")) {
        if (!num || v < 0 || v >= (1L << PAGE_COUNT)) return false;
        if (apply) c.pageMask = uint16_t(v | 1);
        return true;
    }
    if (!strcmp(key, "story")) {                         // News: seconds per headline
        if (!num || v < 4 || v > 60) return false;
        if (apply) c.storySec = uint8_t(v);
        return true;
    }
    if (!strcmp(key, "hold")) {                          // minutes a hand-picked page stays
        if (!num || v < 1 || v > 60) return false;
        if (apply) {
            c.holdMin = uint8_t(v);
            if (manualPage && int32_t(manualUntil - millis()) > int32_t(v * 60000L)) manualUntil = millis() + v * 60000UL;
        }
        return true;
    }
    if (!strcmp(key, "bridge")) {                        // companion bridge host[:port]; empty = none
        const size_t n = strlen(val);
        if (n >= sizeof c.bridge) return false;
        for (size_t i = 0; i < n; i++)
            if (!isalnum((unsigned char)val[i]) && !strchr(".-:", val[i])) return false;
        if (apply) snprintf_P(c.bridge, sizeof c.bridge, PSTR("%s"), val);
        return true;
    }
    // feed settings (sent to the relay): printable ASCII, fixed buffers
    auto text = [&](char* dst, size_t n) {
        const size_t len = strlen(val);
        if (len >= n) return false;
        for (size_t i = 0; i < len; i++) if (val[i] < 0x20 || val[i] > 0x7E) return false;
        if (apply) { snprintf_P(dst, n, PSTR("%s"), val); g_bridge.relayChanged(); }
        return true;
    };
    if (!strcmp(key, "feeds")) return val[0] && text(c.feeds, sizeof c.feeds);
    if (!strcmp(key, "mkts")) return text(c.mkts, sizeof c.mkts);
    if (!strcmp(key, "team")) return val[0] && text(c.team, sizeof c.team);
    if (!strcmp(key, "btc")) {
        if (!num || (v != 0 && v != 1)) return false;
        if (apply) { c.btc = v == 1; g_bridge.relayChanged(); }
        return true;
    }
    if (!strcmp(key, "nage")) {
        if (!num || v < 1 || v > 168) return false;
        if (apply) { c.newsAge = uint8_t(v); g_bridge.relayChanged(); }
        return true;
    }
    if (!strcmp(key, "relay")) {                         // cloud relay host (relay/worker.js); empty = none
        const size_t n = strlen(val);
        if (n >= sizeof c.relay) return false;
        for (size_t i = 0; i < n; i++)
            if (!isalnum((unsigned char)val[i]) && !strchr(".-:", val[i])) return false;
        if (apply) { snprintf_P(c.relay, sizeof c.relay, PSTR("%s"), val); g_bridge.relayChanged(); }
        return true;
    }
    // Duino-Coin mining (off by default). Username, key and rig name go into the pool's comma-separated
    // protocol, so commas and control characters are refused.
    auto ducoText = [&](char* dst, size_t n, bool needed) {
        const size_t len = strlen(val);
        if (len >= n || (needed && !len)) return false;
        for (size_t i = 0; i < len; i++) if (val[i] <= 0x20 || val[i] > 0x7E || val[i] == ',') return false;
        if (apply) { snprintf_P(dst, n, PSTR("%s"), val); g_miner.settingsChanged(); }
        return true;
    };
    if (!strcmp(key, "duser")) return ducoText(c.ducoUser, sizeof c.ducoUser, false);
    if (!strcmp(key, "dkey")) return ducoText(c.ducoKey, sizeof c.ducoKey, false);
    if (!strcmp(key, "drig")) return ducoText(c.ducoRig, sizeof c.ducoRig, true);
    if (!strcmp(key, "mine")) {
        if (!num || (v != 0 && v != 1)) return false;
        if (apply) {
            c.mineOn = v == 1;
            if (c.mineOn) {                               // put the Mining page into the rotation
                for (uint8_t i = 0; i < PAGE_COUNT; i++) if (PAGES[i] == &miningScreen) c.pageMask |= uint16_t(1u << i);
            }
            g_miner.settingsChanged();
        }
        return true;
    }
    if (!strcmp(key, "night")) {                         // "from-to", equal = off
        int f = -1, t = -1;
        if (sscanf(val, "%d-%d", &f, &t) != 2 || f < 0 || f > 23 || t < 0 || t > 23) return false;
        if (apply) { c.nightFrom = uint8_t(f); c.nightTo = uint8_t(t); }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- serial commands
// One JSON object per line:
//   {"cmd":"setup","ssid":..,"pass":..,"city":..}  (tools/usb_setup.py - password never echoed)
//   {"cmd":"status"}   {"cmd":"bench"}   {"cmd":"page","name":"idle","t":"up|down|fade"}
//   {"cmd":"glow","pct":60}   (ambient glow calibration, saved)
//   {"cmd":"otakey"}   (wireless update key, used by tools/ota_update.py)
// Called from inside the web server while an update streams in: keep the update screen drawn.
static void otaProgress() {
    g_ui.show(&updateScreen, Transition::None, -1, 0, brightness255());
    g_ui.loop(millis());
}

static void serialCommands() {
    static char line[300];
    static size_t n = 0;
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\r') continue;
        if (ch != '\n') { if (n < sizeof line - 1) line[n++] = ch; continue; }
        line[n] = 0; n = 0;
        if (line[0] != '{') continue;
        JsonDocument doc;
        if (deserializeJson(doc, line)) { Serial.println(F("[cmd] bad json")); continue; }
        const char* cmd = doc["cmd"] | "";
        if (!strcmp(cmd, "setup")) {
            bool ok = g_net.applySetup(doc["ssid"] | "", doc["pass"] | "", doc["city"] | "", doc["tz"] | "",
                                       doc["h24"] | -1, doc["theme"] | "");
            Serial.println(ok ? F("[setup] saved, restarting") : F("[setup] save failed"));
        } else if (!strcmp(cmd, "otakey")) {           // USB only: the wireless update key
            Serial.printf_P(PSTR("[otakey] %s\n"), g_net.otaKey());
        } else if (!strcmp(cmd, "status")) {
            Serial.printf_P(PSTR("[status] mode %u ip %s ssid %s heap %u screen %s scroll %u\n"), unsigned(g_state.net.mode),
                          g_state.net.ip, g_state.net.ssid, ESP.getFreeHeap(),
                          g_ui.current() ? g_ui.current()->name() : "-", lcd.scroll());
        } else if (!strcmp(cmd, "bench")) {
            uint32_t us = g_ui.benchFullFrame(3);
            uint32_t other = us - g_ui.benchPaint - g_ui.benchSwap - g_ui.benchSpi;
            Serial.printf_P(PSTR("[bench] %s full frame %lu ms = paint %lu + swap %lu + spi %lu + other %lu (glow %u%%)\n"),
                          g_ui.current() ? g_ui.current()->name() : "-", (unsigned long)(us / 1000),
                          (unsigned long)(g_ui.benchPaint / 1000), (unsigned long)(g_ui.benchSwap / 1000),
                          (unsigned long)(g_ui.benchSpi / 1000), (unsigned long)(other / 1000), g_glowPct);
        } else if (!strcmp(cmd, "page")) {
            bool ok = requestPage(doc["name"] | "", doc["t"] | "up");
            Serial.println(ok ? F("[page] ok") : F("[page] unknown page"));
        } else if (!strcmp(cmd, "rotate")) {
            int sec = doc["s"] | -1;
            if (sec >= 0 && sec <= 600) {
                g_config.get().rotateSec = uint16_t(sec);
                g_config.save();
                rotSince = millis();
                Serial.printf_P(PSTR("[rotate] %d s%s\n"), sec, sec ? "" : " (off)");
            }
        } else if (!strcmp(cmd, "focus")) {
            bool ok = g_focus.command(doc["a"] | "", doc["s"] | 0);
            if (ok) requestPage("focus", "up");
            Serial.println(ok ? F("[focus] ok") : F("[focus] unknown action"));
        } else if (!strcmp(cmd, "night")) {
            int f = doc["from"] | -1, t = doc["to"] | -1;
            if (f >= 0 && f < 24 && t >= 0 && t < 24) {
                g_config.get().nightFrom = uint8_t(f);
                g_config.get().nightTo = uint8_t(t);
                g_config.save();
                Serial.printf_P(PSTR("[night] %s\n"), f == t ? "screensaver off" : "screensaver hours set");
            }
        } else if (!strcmp(cmd, "glow")) {
            int p = doc["pct"] | -1;
            if (p >= 0 && p <= 200) {
                g_glowPct = uint8_t(p);
                g_config.get().glowPct = uint8_t(p);
                g_config.save();
                g_ui.invalidateAll();
                Serial.printf_P(PSTR("[glow] %d%%\n"), p);
            }
        }
    }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
    Serial.begin(115200);
    delay(20);
    Serial.printf_P(PSTR("\n[CAIDEN] OS %s  reset: %s\n"), CAIDEN_FW_VERSION, ESP.getResetReason().c_str());

    PanelConfig pc;                  // verified on hardware in M1 (HARDWARE_INVESTIGATION.md)
    lcd.begin(pc);
    lcd.setBacklight(0);
    g_ui.begin(&renderer);

    g_storage.begin();
    g_config.load();
    g_glowPct = g_config.get().glowPct;
    g_ui.setTheme(themeFromName(g_config.get().theme));

    g_ui.show(&bootScreen, Transition::None);
    g_ui.loop(millis());             // render splash while backlight is off
    for (int i = 0; i <= 20; i++) { lcd.setBacklight(uint16_t(brightness255()) * i / 20); delay(12); }
    g_ui.setBrightness(brightness255());

    g_net.begin();
    g_net.onOtaProgress = otaProgress;
    g_clock.begin(g_config.get().tz);
    g_focus.begin();
    pinMode(4, INPUT_PULLUP);        // pad: no signal on this unit (M1), watched passively only
    Serial.printf_P(PSTR("[CAIDEN] heap after init %u\n"), ESP.getFreeHeap());
}

// ---------------------------------------------------------------- weather / air alerts
static void updateAlerts() {
    const WeatherData& w = g_state.weather;
    const AirData& a = g_state.air;
    AlertData& al = g_state.alert;
    uint8_t kind = 0;
    char txt[48] = "";
    struct tm t;
    const bool tv = g_clock.now(t);
    if (w.valid && w.code >= 95) { kind = 1; snprintf_P(txt, sizeof txt, PSTR("Thunderstorm now")); }
    if (!kind && w.valid && w.rainHValid && tv) {
        uint8_t best = 0; int when = 0;
        for (int h = 0; h < 3 && t.tm_hour + h < 24; h++)
            if (w.rainH[t.tm_hour + h] > best) { best = w.rainH[t.tm_hour + h]; when = h; }
        if (best >= 60) {
            kind = 2;
            if (when == 0) snprintf_P(txt, sizeof txt, PSTR("Rain likely this hour (%u%%)"), best);
            else snprintf_P(txt, sizeof txt, PSTR("Rain likely in %d h (%u%%)"), when, best);
        }
    }
    if (!kind && a.valid && a.naqi >= 301) { kind = 3; snprintf_P(txt, sizeof txt, PSTR("Very poor air: AQI %d"), a.naqi); }
    if (!kind && w.valid && w.tMax >= 40) { kind = 4; snprintf_P(txt, sizeof txt, PSTR("Heat today: up to %d\xC2\xB0"), int(lroundf(w.tMax))); }
    if (!kind && a.valid && a.naqi >= 201) { kind = 3; snprintf_P(txt, sizeof txt, PSTR("Poor air: AQI %d"), a.naqi); }
    if (!kind && w.valid && tv && t.tm_hour >= 10 && t.tm_hour < 16 && w.uv >= 8) { kind = 5; snprintf_P(txt, sizeof txt, PSTR("Very high UV (%d): use shade"), int(lroundf(w.uv))); }
    if (!kind && w.valid && w.wind >= 40) { kind = 6; snprintf_P(txt, sizeof txt, PSTR("Strong wind: %d km/h"), int(lroundf(w.wind))); }
    if (!kind && w.valid && tv && t.tm_hour < 10 && w.tMin <= 8) { kind = 7; snprintf_P(txt, sizeof txt, PSTR("Cold morning: %d\xC2\xB0"), int(lroundf(w.tMin))); }
    if (kind == al.kind && !strcmp(txt, al.text)) return;
    al.kind = kind;
    snprintf_P(al.text, sizeof al.text, PSTR("%s"), txt);
    g_ui.setAlert(kind ? al.text : nullptr, kind);
    Serial.printf_P(PSTR("[alert] %s\n"), kind ? al.text : "none");
}

void loop() {
    uint32_t now = millis();
    serialCommands();
    g_net.loop();
    g_clock.loop();
    g_focus.loop();
    choosePage(now);
    g_ui.loop(now);
    if (!g_ui.transitioning()) {
        g_weather.loop(g_net.online() && g_clock.valid());
        g_bridge.loop(g_net.online());
        g_lan.loop(g_net.online());          // Home network page: the clock's own device count
        g_miner.loop(g_net.online());        // Duino-Coin hobby miner: spare time only, off by default
    }

    static uint32_t lastAlert = 0;
    if (now - lastAlert > 30000 || !lastAlert) { lastAlert = now | 1; updateAlerts(); }
    static uint32_t lastLog = 0;
    if (now - lastLog > 60000) {
        lastLog = now;
        Serial.printf_P(PSTR("[sys] heap %u frag %u%% maxblk %u frame %lums\n"), ESP.getFreeHeap(),
                      ESP.getHeapFragmentation(), ESP.getMaxFreeBlockSize(),
                      (unsigned long)(g_ui.lastFrameUs() / 1000));
    }
}
