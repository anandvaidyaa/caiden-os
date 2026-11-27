#include "OrbitScreen.h"
#include <math.h>
#include <string.h>
#include "../ui/Lumen.h"
#include "../services/ClockService.h"
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

using namespace lumen;

static const float CX = 120, CY = 120, R = 109, R_IN = 93, TH_IN = 3;
static const int16_t CLOCK_BASE = 139;
static const uint32_t FADE_HALF_MS = 150;
static const float DEG = 0.01745329f;

static float ringAngle(int minutes) { return 90.f + 360.f * minutes / 1440.f; }   // midnight bottom, noon top

static int parseHM(const char* s, int fallback) {
    if (!s || strlen(s) < 5 || s[2] != ':') return fallback;
    for (int i : {0, 1, 3, 4}) if (!isdigit(uint8_t(s[i]))) return fallback;
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) return fallback;
    return h * 60 + m;
}


void OrbitScreen::background(Canvas& c, const Theme& t) {
    // Orbit's own light pools: a soft accent light behind the dial, a violet glow below.
    static Light dark[2], light[2];
    Light* L = t.dark ? dark : light;
    L[0] = {120, 118, 128, t.dark ? t.accent : rgb(0xAFC4FF), uint8_t(t.dark ? 34 : 90)};
    L[1] = {120, 250, 110, t.accent2, uint8_t(t.dark ? 40 : 60)};
    paintBackground(c, t, L, 2);
}

void OrbitScreen::enter(uint32_t now) {
    (void)now;
    struct tm tm;
    lastMin_ = g_clock.now(tm) ? tm.tm_min : -1;
    char hh[4], mm[4]; bool am;
    g_clock.hhmm(hh, mm, g_config.get().h24, am);
    strcpy(shownHH_, hh); strcpy(shownMM_, mm);
    fading_ = false;
    wxVer_ = g_state.weather.version; airVer_ = g_state.air.version; netVer_ = g_state.net.version;
    bars_ = wifiBars(); mode_ = uint8_t(g_state.net.mode);
}

void OrbitScreen::update(uint32_t now, DirtySet& d) {
    struct tm tm;
    if (g_clock.now(tm) && tm.tm_min != lastMin_) {
        bool first = lastMin_ < 0;
        lastMin_ = tm.tm_min;
        char hh[4], mm[4]; bool am;
        g_clock.hhmm(hh, mm, g_config.get().h24, am);
        if (!first) { strcpy(prevHH_, shownHH_); strcpy(prevMM_, shownMM_); fading_ = true; fadeAt_ = now; }
        strcpy(shownHH_, hh); strcpy(shownMM_, mm);
        d.addAll();                       // sun moves, daylight arc grows, digits change
        return;
    }
    if (fading_) {
        ClockGeom g = clockLayout(FONT_L_CLOCK_L, CX, CLOCK_BASE, 2);   // the widest (4-digit) layout
        d.add(Rect(int16_t(g.x0 - 4), int16_t(g.capTop - 2), int16_t(g.x1 - g.x0 + 8), int16_t(CLOCK_BASE - g.capTop + 6)));
        if (now - fadeAt_ >= 2 * FADE_HALF_MS) fading_ = false;
    }
    // colon: soft fade 100 % -> 35 % -> 100 % over 2 s (8 steps), two small regions
    uint8_t step = (now % 2000) * 8 / 2000;
    if (step != colonStep_) {
        colonStep_ = step;
        float ph = (now % 2000) / 2000.f;
        colonAlpha_ = uint8_t(255 * (0.675f + 0.325f * cosf(ph * 6.2832f)));
        ClockGeom g = clockLayout(FONT_L_CLOCK_L, CX, CLOCK_BASE, uint8_t(strlen(shownHH_)));
        int16_t r = int16_t(g.colonR + 3);
        d.add(Rect(int16_t(g.colonX - r), int16_t(g.colonY0 - r), int16_t(2 * r), int16_t(g.colonY1 - g.colonY0 + 2 * r)));
    }
    if (g_state.net.version != netVer_) {
        netVer_ = g_state.net.version;
        if (uint8_t(g_state.net.mode) != mode_) { mode_ = uint8_t(g_state.net.mode); d.addAll(); }
        uint8_t b = wifiBars();
        if (b != bars_) { bars_ = b; d.add(Rect(8, 6, 28, 28)); }
    }
    if (g_state.weather.version != wxVer_ || g_state.air.version != airVer_) {
        wxVer_ = g_state.weather.version; airVer_ = g_state.air.version;
        d.addAll();
    }
    // bridge indicators: badge (top right) and music equaliser (bottom left), small regions only
    const BridgeData& br = g_state.bridge;
    if (br.version != brVer_) {
        brVer_ = br.version;
        uint8_t n = br.online ? br.notify : 0;
        bool pl = br.online && br.sp.on && br.sp.playing;
        if (n != notify_) { notify_ = n; d.add(Rect(206, 4, 34, 32)); }
        if (pl != playing_) { playing_ = pl; d.add(Rect(8, 206, 52, 20)); }
    }
    const uint8_t mine = uint8_t((miningShown() ? 1 : 0) | (miningActive() ? 2 : 0));
    if (mine != mineShown_) { mineShown_ = mine; d.add(Rect(8, 206, 52, 20)); }
    if ((playing_ || (mine & 2)) && now - lastEq_ >= 83) {   // equaliser and pickaxe ~12 fps
        lastEq_ = now;
        eqPhase_ = (now % 2400) / 2400.f;
        d.add(Rect(8, 206, 52, 20));
    }
}

void OrbitScreen::render(Canvas& c, const Theme& t) {
    const Config& cfg = g_config.get();
    const WeatherData& w = g_state.weather;
    const AirData& a = g_state.air;
    struct tm tm;
    const bool timeOk = g_clock.now(tm);
    const int nowMin = timeOk ? tm.tm_hour * 60 + tm.tm_min : 12 * 60;

    // dial disc (subtle glass) behind everything inside the ring
    c.setOpacity(t.dark ? 140 : 179);
    c.fillCircle(CX, CY, R - 9, t.dark ? t.bg : t.surface);
    c.setOpacity(255);

    // 24 hour ticks (majors every 6 h); endpoints computed once
    static float tick[24][4];
    static bool tickInit = false;
    if (!tickInit) {
        for (int h = 0; h < 24; h++) {
            float an = (h * 15.f - 90) * DEG, r0 = R - (h % 6 == 0 ? 11 : 8), r1 = R - 6;
            tick[h][0] = CX + cosf(an) * r0; tick[h][1] = CY + sinf(an) * r0;
            tick[h][2] = CX + cosf(an) * r1; tick[h][3] = CY + sinf(an) * r1;
        }
        tickInit = true;
    }
    for (int h = 0; h < 24; h++) {
        bool major = h % 6 == 0;
        c.setOpacity(major ? 242 : 140);
        c.line(tick[h][0], tick[h][1], tick[h][2], tick[h][3], major ? 1.6f : 1.1f, major ? t.text2 : t.text3);
    }

    // sun-path ring: track, daylight span, elapsed daylight, sunrise/sunset pips, sun / moon
    int srM = parseHM(w.valid ? w.sunrise : nullptr, 6 * 60 + 30);
    int ssM = parseHM(w.valid ? w.sunset : nullptr, 18 * 60);
    if (srM >= ssM) { srM = 6 * 60 + 30; ssM = 18 * 60; }
    const float sr = ringAngle(srM), ss = ringAngle(ssM), nowA = ringAngle(nowMin);
    c.setOpacity(230);
    c.ring(CX, CY, R, 3, t.surfaceHi);
    c.setOpacity(t.dark ? 153 : 128);
    c.arc(CX, CY, R, 3, sr, ss, t.text3, false);
    c.setOpacity(255);
    const bool daytime = nowMin >= srM && nowMin <= ssM;
    if (timeOk && nowMin > srM) c.arcGradient(CX, CY, R, 3, sr, daytime ? nowA : ss, t.accent2, t.accent);
    for (float an : {sr, ss}) c.fillCircle(CX + (R + 4.5f) * cosf(an * DEG), CY + (R + 4.5f) * sinf(an * DEG), 1.9f, t.sun);
    if (timeOk) {
        const float tx = CX + (R - 1.5f) * cosf(nowA * DEG), ty = CY + (R - 1.5f) * sinf(nowA * DEG);
        if (daytime) {
            c.glow(tx, ty, 18, t.sun2, 150);
            c.fillCircle(tx, ty, 4.2f, t.sun);
            c.fillCircle(tx - 0.6f, ty - 0.6f, 2.2f, rgb(0xFFF4D6));
        } else {
            c.glow(tx, ty, 14, t.moon, 70);
            c.fillCircle(tx, ty, 3.6f, t.moon);
        }
    }

    // inner gauges: left = temperature 10..45 C, right = six NAQI bands
    {
        const float aLo = 128, aHi = 232;
        c.setOpacity(230);
        c.arc(CX, CY, R_IN, TH_IN, aLo, aHi, t.surfaceHi);
        c.setOpacity(255);
        if (w.valid) {
            float frac = (w.temp - 10.f) / 35.f;
            frac = frac < 0.02f ? 0.02f : (frac > 1 ? 1 : frac);
            float aEnd = aLo + (aHi - aLo) * frac;
            c.arcGradient(CX, CY, R_IN, TH_IN, aLo, aEnd, t.highlight, t.sun);
            float px = CX + (R_IN - 1.5f) * cosf(aEnd * DEG), py = CY + (R_IN - 1.5f) * sinf(aEnd * DEG);
            c.glow(px, py, 12, t.sun, 140);
            c.fillCircle(px, py, 3.4f, t.text);
        }
    }
    {
        const float bLo = 52, bHi = -52, seg = (bLo - bHi) / 6.f;
        const bool ok = a.valid && a.naqi >= 0;
        const uint8_t bi = ok ? aqiBand(a.naqi) : 255;
        for (int i = 0; i < 6; i++) {
            float a1 = bLo - i * seg - 1.8f, a0 = bLo - (i + 1) * seg + 1.8f;
            c.setOpacity(i == bi ? 255 : (t.dark ? 71 : 89));
            c.arc(CX, CY, R_IN, i == bi ? TH_IN : 2, a0, a1, t.aqi[i], false);
        }
        c.setOpacity(255);
        if (ok) {
            static const int BP[7] = {0, 50, 100, 200, 300, 400, 500};
            int v = a.naqi > 500 ? 500 : a.naqi;
            float pos = bLo - seg * (bi + float(v - BP[bi]) / float(BP[bi + 1] - BP[bi]));
            float mx = CX + (R_IN - 1.5f) * cosf(pos * DEG), my = CY + (R_IN - 1.5f) * sinf(pos * DEG);
            c.glow(mx, my, 12, t.aqi[bi], 140);
            c.fillCircle(mx, my, 3.4f, t.text);
        }
    }

    // centre stack: date, clock, weather + AQI row, conditions, city
    char date[32];
    if (timeOk) {
        char wd[12], dm[12];
        strftime(wd, sizeof wd, "%A", &tm);
        char mon[6];
        strftime(mon, sizeof mon, "%b", &tm);
        snprintf_P(dm, sizeof dm, PSTR("%d %s"), tm.tm_mday, mon);
        snprintf_P(date, sizeof date, PSTR("%s  \xC2\xB7  %s"), wd, dm);
        for (char* p = date; *p; p++) *p = toupper(uint8_t(*p));
    } else {
        snprintf_P(date, sizeof date, PSTR("%s"), g_state.net.mode == NetMode::Connecting ? "CONNECTING" : "SYNCING TIME");
    }
    label(c, int16_t(CX), 76, date, t.text2, Align::Center);

    {   // clock with changed-digit cross-fade (old fades out 150 ms, new fades in 150 ms)
        const char* hh = shownHH_; const char* mm = shownMM_;
        uint8_t al[4] = {255, 255, 255, 255};
        if (fading_) {
            uint32_t el = millis() - fadeAt_;
            bool phase1 = el < FADE_HALF_MS;
            uint8_t k = phase1 ? uint8_t(255 - 255 * el / FADE_HALF_MS)
                               : (el < 2 * FADE_HALF_MS ? uint8_t(255 * (el - FADE_HALF_MS) / FADE_HALF_MS) : 255);
            if (phase1) { hh = prevHH_; mm = prevMM_; }
            bool relayout = strlen(prevHH_) != strlen(shownHH_);
            char o[6], n[6];
            snprintf_P(o, sizeof o, PSTR("%s%s"), prevHH_, prevMM_);
            snprintf_P(n, sizeof n, PSTR("%s%s"), shownHH_, shownMM_);
            for (int i = 0; i < 4 && n[i]; i++) if (relayout || o[i] != n[i]) al[i] = k;
        }
        if (timeOk) clock(c, t, CX, CLOCK_BASE, hh, mm, FONT_L_CLOCK_L, al, colonAlpha_);
        else clock(c, t, CX, CLOCK_BASE, "--", "--", FONT_L_CLOCK_L, nullptr, colonAlpha_);
    }

    {   // weather + AQI row
        const int16_t rowY = 167;
        char temp[12], aq[8];
        if (w.valid) snprintf_P(temp, sizeof temp, PSTR("%d\xC2\xB0"), int(lroundf(w.temp)));
        else strcpy(temp, "--\xC2\xB0");
        const bool aok = a.valid && a.naqi >= 0;
        if (aok) snprintf_P(aq, sizeof aq, PSTR("%d"), a.naqi); else strcpy(aq, "--");
        float wi = Canvas::textWidth(temp, FONT_L_NUM_M), ai = Canvas::textWidth(aq, FONT_L_NUM_M);
        float total = 22 + 4 + wi + 18 + 10 + ai;
        float x = CX - total / 2;
        weatherIcon(c, t, w.valid ? wxFromWmo(w.code, w.isDay) : Wx::Unknown, x + 10, rowY - 8, 22, false);
        c.text(int16_t(x + 26), rowY, temp, FONT_L_NUM_M, t.text);
        float x2 = x + 26 + wi + 14;
        c.fillCircle(x2 + 4, rowY - 8, 3.6f, aok ? t.aqi[aqiBand(a.naqi)] : t.text3);
        c.text(int16_t(x2 + 13), rowY, aq, FONT_L_NUM_M, aok ? t.text : t.text3);
    }

    {   // conditions line + city
        // At this height the two inner gauges (radius 93) leave about 124 px between them (114 with a
        // 5 px gap); a wider line ran over both. Long pairs drop words first ("Clear · Moderate air"
        // becomes "Clear · Moderate", then "Moderate air"; the weather icon above still shows the sky).
        static const int16_t LINE_W = 114;
        char line[48];
        if (cfg.geoFailed) snprintf_P(line, sizeof line, PSTR("City not found"));
        else if (w.valid && a.valid && a.naqi >= 0) {
            const char* wx = wxWord(w.code, w.isDay);
            const char* air = aqiLongName(aqiBand(a.naqi));
            snprintf_P(line, sizeof line, PSTR("%s \xC2\xB7 %s air"), wx, air);
            if (Canvas::textWidth(line, FONT_L_BODY) > LINE_W) snprintf_P(line, sizeof line, PSTR("%s \xC2\xB7 %s"), wx, air);
            if (Canvas::textWidth(line, FONT_L_BODY) > LINE_W) snprintf_P(line, sizeof line, PSTR("%s air"), air);
            if (Canvas::textWidth(line, FONT_L_BODY) > LINE_W) snprintf_P(line, sizeof line, PSTR("%s"), air);
        }
        else if (w.valid) snprintf_P(line, sizeof line, PSTR("%s"), wxWord(w.code, w.isDay));
        else snprintf_P(line, sizeof line, PSTR("%s"), g_state.net.mode == NetMode::Online ? "Fetching weather" : "Waiting for Wi-Fi");
        char fit[48];
        Canvas::ellipsize(line, FONT_L_BODY, LINE_W, fit, sizeof fit);
        c.text(int16_t(CX), 187, fit, FONT_L_BODY, t.text2, Align::Center);
        char city[34];
        snprintf_P(city, sizeof city, PSTR("%s"), cfg.city);
        for (char* p = city; *p; p++) *p = toupper(uint8_t(*p));
        char cityFit[40];
        Canvas::ellipsize(city, FONT_L_LABEL, 100, cityFit, sizeof cityFit);
        label(c, int16_t(CX), 205, cityFit, t.text3, Align::Center);
    }

    // corners: Wi-Fi, notifications, music, AM/PM
    iconWifi(c, t, 21, 22, 16, wifiBars(), t.text2, t.text3);
    iconBell(c, 219, 22, 16, t.text2);
    const BridgeData& br = g_state.bridge;
    if (br.online && br.notify) badge(c, 226, 16, br.notify, t.accent2, t.bg, true);
    const bool eq = br.online && br.sp.on && br.sp.playing;
    if (eq) eqBars(c, 13, 218, 12, t.highlight, eqPhase_);
    if (miningShown()) mineBadge(c, t, eq ? 36 : 13, 218, miningActive(), (millis() % 700) / 700.f);
    if (!cfg.h24 && timeOk) label(c, 227, 224, tm.tm_hour < 12 ? "AM" : "PM", t.text3, Align::Right);
}
