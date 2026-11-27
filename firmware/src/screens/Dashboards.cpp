#include "Dashboards.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include "../ui/Lumen.h"
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

using namespace lumen;

#define FS(s) flashStr(_t, sizeof _t, PSTR(s))

static const float DEG = 0.01745329f;

// ---------------------------------------------------------------- shared
void DashboardScreen::enter(uint32_t now) {
    (void)now;
    time_t tt = time(nullptr);
    struct tm tm;
    localtime_r(&tt, &tm);
    lastMin_ = tm.tm_min;
    wxVer_ = g_state.weather.version; airVer_ = g_state.air.version; netVer_ = g_state.net.version;
}

void DashboardScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    time_t tt = time(nullptr);
    struct tm tm;
    localtime_r(&tt, &tm);
    if (tm.tm_min != lastMin_) {
        lastMin_ = tm.tm_min;
        d.add(HEADER_TIME_RECT);
        if (tm.tm_min % 5 == 0) d.addAll();      // chart now-marker / past-future split advance
    }
    if (g_state.net.version != netVer_) { netVer_ = g_state.net.version; d.add(HEADER_TIME_RECT); }
    if (g_state.weather.version != wxVer_ || g_state.air.version != airVer_) {
        wxVer_ = g_state.weather.version; airVer_ = g_state.air.version;
        d.addAll();
    }
}

// Dashboard ambient: the theme's two light pools pulled into the corners (r ~120) + extra.
static void dashAmbient(Canvas& c, const Theme& t, Light* L, const Light* extra) {
    L[0] = {214, 6, 124, t.lights[0].color, t.lights[0].strength};
    L[1] = {8, 240, 116, t.lights[1].color, t.lights[1].strength};
    uint8_t n = 2;
    if (extra) L[n++] = *extra;
    paintBackground(c, t, L, n);
}


static const char* const WDAY[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static uint16_t mix(uint16_t a, uint16_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return pack565(uint8_t(r8(a) + (r8(b) - r8(a)) * t), uint8_t(g8(a) + (g8(b) - g8(a)) * t),
                   uint8_t(b8(a) + (b8(b) - b8(a)) * t));
}

// ---------------------------------------------------------------- weather
// CAIDEN OS 3 (design2/render_v3.py v3_weather): sky atmosphere, 76 px temperature, quiet metric
// row on a hairline, hourly ribbon along the bottom edge.
static const float WX_ICON_X = 186, WX_ICON_Y = 66, WX_ICON_S = 62;

void WeatherScreen::background(Canvas& c, const Theme& t) {
    static Light L[6];
    const WeatherData& w = g_state.weather;
    Wx k = w.valid ? wxFromWmo(w.code, w.isDay) : Wx::Unknown;
    uint8_t n = 0;
    L[n++] = {60, -10, 150, t.dark ? t.skyTop : t.skyBot, uint8_t(t.dark ? 70 : 60)};      // sky from above
    L[n++] = {8, 240, 116, t.lights[1].color, t.lights[1].strength};
    if (k == Wx::ClearDay || k == Wx::PartlyDay) {
        L[n++] = {206, 34, 120, t.sun2, uint8_t(t.dark ? 60 : 55)};
        // halos of the hero illustration (same proportions as the icon's own glow())
        const float sx = k == Wx::ClearDay ? WX_ICON_X : WX_ICON_X - 0.14f * WX_ICON_S;
        const float sy = k == Wx::ClearDay ? WX_ICON_Y : WX_ICON_Y - 0.14f * WX_ICON_S;
        const float ss = k == Wx::ClearDay ? WX_ICON_S : 0.7f * WX_ICON_S;
        L[n++] = {int16_t(sx), int16_t(sy), int16_t(0.8f * ss), t.sun2, uint8_t(t.dark ? 70 : 90)};
        L[n++] = {int16_t(sx), int16_t(sy), int16_t(0.44f * ss), t.sun, 90};
    } else if (k == Wx::ClearNight || k == Wx::PartlyNight) {
        L[n++] = {int16_t(WX_ICON_X), int16_t(WX_ICON_Y), 56, t.moon, 45};
    } else if (k == Wx::Rain || k == Wx::Storm) {
        L[n++] = {200, 40, 120, t.rain, uint8_t(t.dark ? 50 : 45)};
    } else {
        L[n++] = {200, 40, 120, t.lights[0].color, t.lights[0].strength};
    }
    paintBackground(c, t, L, n);
}

static const char* uvWord(float uv) {
    return uv < 3 ? "LOW" : uv < 6 ? "MODERATE" : uv < 8 ? "HIGH" : uv < 11 ? "V HIGH" : "EXTREME";
}

void WeatherScreen::render(Canvas& c, const Theme& t) {
    char _t[40];
    const WeatherData& w = g_state.weather;
    const Config& cfg = g_config.get();
    header(c, t, cfg.city, nullptr, t.highlight, wifiBars());
    weatherIcon(c, t, w.valid ? wxFromWmo(w.code, w.isDay) : Wx::Unknown, WX_ICON_X, WX_ICON_Y, WX_ICON_S, false);

    // hero temperature: 76 px gradient digits + drawn degree ring
    char buf[48];
    if (w.valid) snprintf_P(buf, sizeof buf, PSTR("%d"), int(lroundf(w.temp))); else strcpy(buf, "--");
    const int16_t tw = hero(c, t, M - 3, 126, buf, FONT_L_CLOCK_XL);
    degree(c, M - 3 + tw + 8, 76, 5.5f, t.text);
    if (w.valid) {
        int16_t x = c.text(M, 150, wxWord(w.code, w.isDay), FONT_L_TITLE, t.text);
        snprintf_P(buf, sizeof buf, PSTR("Feels %d\xC2\xB0"), int(lroundf(w.feels)));
        c.text(int16_t(M + x + 8), 150, buf, FONT_L_BODY, t.text2);
    } else {
        c.text(M, 150, g_state.net.mode == NetMode::Online ? FS("Fetching weather") : FS("Waiting for Wi-Fi"), FONT_L_BODY, t.text2);
    }

    // quiet metric row: value + small caps label, no box. Third cell: rain chance when it matters, else UV.
    hairline(c, t, 163);
    const float cw = 212 / 3.f;
    const bool rainy = w.valid && w.rainProb >= 30;
    for (int i = 0; i < 3; i++) {
        const int16_t x = int16_t(M + i * cw);
        char v[12] = "--", unit[8] = "", lab[20];
        uint16_t vc = t.text;
        if (i == 0) { if (w.valid) snprintf_P(v, sizeof v, PSTR("%u%%"), w.humidity); flashStr(lab, sizeof lab, PSTR("HUMIDITY")); }
        else if (i == 1) {
            if (w.valid) { snprintf_P(v, sizeof v, PSTR("%d"), int(lroundf(w.wind))); strcpy(unit, "km/h"); }
            snprintf_P(lab, sizeof lab, PSTR("WIND %s"), w.valid ? compass(w.windDir) : "");
        } else if (rainy) {
            snprintf_P(v, sizeof v, PSTR("%u%%"), w.rainProb); flashStr(lab, sizeof lab, PSTR("RAIN"));
            vc = w.rainProb >= 60 ? t.rain : t.text;
        } else {
            if (w.valid) snprintf_P(v, sizeof v, PSTR("%d"), int(lroundf(w.uv)));
            snprintf_P(lab, sizeof lab, PSTR("UV %s"), w.valid ? uvWord(w.uv) : "");
            vc = w.valid && w.uv >= 6 ? t.warn : t.text;
        }
        int16_t vw = c.text(x, 182, v, FONT_L_NUM_S, vc);
        if (unit[0]) c.text(int16_t(x + vw + 3), 182, unit, FONT_L_BODY, t.text3);
        label(c, x, 196, lab, t.text3);
    }

    // hourly ribbon 06:00 -> 21:00 along the bottom: past dimmed, future luminous, now dot, peak
    const float cx0 = M, cx1 = 226, ctop = 214, cbot = 232;
    time_t tt = time(nullptr) + w.utcOffset;      // location-local time (matches the hourly series)
    struct tm tm;
    gmtime_r(&tt, &tm);
    const Rect win = c.window();
    if (!(w.valid && w.hourlyValid) || win.b() <= 200) return;
    float vmin = 1e9f, vmax = -1e9f; int imax = 0;
    for (int i = 0; i < 16; i++) {
        float v = w.hourly[i] / 10.f;
        if (v < vmin) vmin = v;
        if (v > vmax) { vmax = v; imax = i; }
    }
    const float lo = vmin - 1, hi = vmax + 1;
    auto X = [&](float h) { return cx0 + (h - 6) / 15.f * (cx1 - cx0); };
    auto Y = [&](float v) { return cbot - (v - lo) / (hi - lo) * (cbot - ctop); };
    // Catmull-Rom (4 sub-steps per hour), cached per weather update: render() runs once per band.
    static float px[61], py[61];
    static int n = 0;
    static uint16_t cachedVer = 0xFFFF;
    if (cachedVer != w.version) {
        cachedVer = w.version;
        n = 0;
        for (int i = 0; i < 15; i++) {
            float p0x = X(6 + (i ? i - 1 : 0)), p0y = Y(w.hourly[i ? i - 1 : 0] / 10.f);
            float p1x = X(6 + i), p1y = Y(w.hourly[i] / 10.f);
            float p2x = X(6 + i + 1), p2y = Y(w.hourly[i + 1] / 10.f);
            int j3 = i + 2 > 15 ? 15 : i + 2;
            float p3x = X(6 + j3), p3y = Y(w.hourly[j3] / 10.f);
            for (int k = 0; k < 4; k++) {
                float s = k / 4.f, s2 = s * s, s3 = s2 * s;
                px[n] = 0.5f * (2 * p1x + (-p0x + p2x) * s + (2 * p0x - 5 * p1x + 4 * p2x - p3x) * s2 + (-p0x + 3 * p1x - 3 * p2x + p3x) * s3);
                py[n] = 0.5f * (2 * p1y + (-p0y + p2y) * s + (2 * p0y - 5 * p1y + 4 * p2y - p3y) * s2 + (-p0y + 3 * p1y - 3 * p2y + p3y) * s3);
                n++;
            }
        }
        px[n] = X(21); py[n] = Y(w.hourly[15] / 10.f); n++;
    }
    const float nowH = tm.tm_hour + tm.tm_min / 60.f;
    const float nowX = X(nowH < 6 ? 6 : (nowH > 21 ? 21 : nowH));
    // soft area under the future part (3 px strips, one forward walk along the curve)
    int seg = 0;
    for (int x = int(nowX); x < int(px[n - 1]); x += 3) {
        float sx = x + 1.5f;
        while (seg + 2 < n && px[seg + 1] < sx) seg++;
        float yy = py[seg] + (py[seg + 1] - py[seg]) * (sx - px[seg]) / (px[seg + 1] - px[seg] + 1e-6f);
        c.setOpacity(t.dark ? 31 : 26);
        c.fillRect(int16_t(x), int16_t(yy), 3, int16_t(cbot + 4 - yy), t.sun2);
    }
    c.setOpacity(255);
    for (int i = 0; i + 1 < n; i++) {
        float mx = (px[i] + px[i + 1]) / 2;
        if (mx <= nowX) {
            c.setOpacity(115);
            c.line(px[i], py[i], px[i + 1], py[i + 1], 1.4f, t.text3);
            c.setOpacity(255);
        } else {
            c.line(px[i], py[i], px[i + 1], py[i + 1], 1.8f, mix(t.highlight, t.sun, (mx - nowX) / (cx1 - nowX + 1)));
        }
    }
    float pkx = X(6 + imax), pky = Y(vmax);
    c.fillCircle(pkx, pky, 2.0f, t.sun);
    if (nowH >= 6 && nowH <= 21) {
        float ny = Y(w.temp < lo ? lo : (w.temp > hi ? hi : w.temp));
        c.glow(nowX, ny, 9, t.highlight, 140);
        c.fillCircle(nowX, ny, 2.6f, t.text);
    }
    // peak label above the ribbon, left aligned
    const int ph = 6 + imax;
    if (cfg.h24) snprintf_P(buf, sizeof buf, PSTR("PEAK %d\xC2\xB0 AT %02d:00"), int(lroundf(vmax)), ph);
    else snprintf_P(buf, sizeof buf, PSTR("PEAK %d\xC2\xB0 AT %d %s"), int(lroundf(vmax)), ph % 12 ? ph % 12 : 12, ph < 12 ? "AM" : "PM");
    label(c, M, 211, buf, t.text3);
}

// ---------------------------------------------------------------- air quality
void AirScreen::background(Canvas& c, const Theme& t) {
    static Light L[3];
    const AirData& a = g_state.air;
    uint16_t band = a.valid && a.naqi >= 0 ? t.aqi[aqiBand(a.naqi)] : t.text3;
    Light extra = {70, 104, 96, band, uint8_t(t.dark ? 46 : 54)};
    dashAmbient(c, t, L, &extra);
}

static const char* advisory(uint8_t b) {
    static const char* A[6] = {"Clean air \xC2\xB7 great for a walk", "Fine for most \xC2\xB7 sensitive: easy",
                               "Sensitive groups: limit exertion", "Limit long outdoor activity",
                               "Avoid outdoor activity", "Stay indoors \xC2\xB7 use a purifier"};
    return A[b > 5 ? 5 : b];
}

void AirScreen::render(Canvas& c, const Theme& t) {
    const AirData& a = g_state.air;
    const bool ok = a.valid && a.naqi >= 0;
    const uint8_t bi = ok ? aqiBand(a.naqi) : 0;
    const uint16_t band = ok ? t.aqi[bi] : t.text3;
    header(c, t, "Air quality", nullptr, band, wifiBars());

    // hero: six-band NAQI dial (equal angle per band), value in the centre
    const float cx = 72, cy = 104, r = 54, a0 = 135, seg = 270 / 6.f;
    for (int i = 0; i < 6; i++) {
        bool on = ok && i == bi;
        c.setOpacity(on ? 255 : (t.dark ? 77 : 97));
        c.arc(cx, cy, r, on ? 5 : 3, a0 + i * seg + 2, a0 + (i + 1) * seg - 2, t.aqi[i], on);
    }
    c.setOpacity(255);
    char buf[48];
    if (ok) {
        static const int BP[7] = {0, 50, 100, 200, 300, 400, 500};
        int v = a.naqi > 500 ? 500 : a.naqi;
        float pos = a0 + seg * (bi + float(v - BP[bi]) / float(BP[bi + 1] - BP[bi]));
        float mx = cx + (r - 2.5f) * cosf(pos * DEG), my = cy + (r - 2.5f) * sinf(pos * DEG);
        c.glow(mx, my, 13, band, 150);
        c.fillCircle(mx, my, 4.0f, t.text);
    }
    label(c, int16_t(cx), 86, "NAQI", t.text3, Align::Center);
    if (ok) snprintf_P(buf, sizeof buf, PSTR("%d"), a.naqi); else strcpy(buf, "--");
    hero(c, t, int16_t(cx), 120, buf, FONT_L_NUM_L, Align::Center);
    const char* bname = ok ? aqiShortName(bi) : "WAITING";
    int16_t bw_ = Canvas::textWidth(bname, FONT_L_LABEL, 1);
    label(c, int16_t(cx), bw_ > 76 ? 150 : 139, bname, band, Align::Center);   // long names clear the arc ends

    // right column: pollutants vs the CPCB 24 h standards
    const float x0 = 140, x1 = 226, bw = x1 - x0;
    struct P { int16_t y; const char* name; float val; int lim; };
    const P ps[2] = {{50, "PM2.5", a.pm25, 60}, {104, "PM10", a.pm10, 100}};
    for (const P& p : ps) {
        label(c, int16_t(x0), p.y, p.name, t.text3);
        snprintf_P(buf, sizeof buf, PSTR("/ %d"), p.lim);
        label(c, int16_t(x1), p.y, buf, t.text3, Align::Right);
        if (ok) snprintf_P(buf, sizeof buf, PSTR("%d"), int(lroundf(p.val))); else strcpy(buf, "--");
        int16_t vw = c.text(int16_t(x0 - 1), int16_t(p.y + 25), buf, FONT_L_NUM_M, t.text);
        c.text(int16_t(x0 + vw + 3), int16_t(p.y + 25), "\xC2\xB5g/m\xC2\xB3", FONT_L_BODY, t.text3);
        c.setOpacity(217);
        c.fillRoundRect(x0, p.y + 33, bw, 4, 2, t.surfaceHi);
        c.setOpacity(255);
        if (ok) {
            float frac = p.val / p.lim;
            frac = frac > 1 ? 1 : (frac < 0.03f ? 0.03f : frac);
            c.fillRoundRect(x0, p.y + 33, bw * frac, 4, 2, band);
            c.fillCircle(x0 + bw * frac - 2, p.y + 35, 3.2f, t.text);
        }
    }

    // advisory
    iconLeaf(c, t, M + 6, 167, 12, band);
    char fit[48];
    Canvas::ellipsize(ok ? advisory(bi) : "Fetching air quality", FONT_L_BODY, 212 - 18, fit, sizeof fit);
    c.text(M + 18, 171, fit, FONT_L_BODY, t.text2);

    // 24 h trend: bars coloured by band, last bar = now
    label(c, M, 189, "LAST 24 H", t.text3);
    if (a.histCount == 24) {
        snprintf_P(buf, sizeof buf, PSTR("PEAK %d \xC2\xB7 %02u:00"), a.peak, a.peakHour);
        label(c, 226, 189, buf, t.text3, Align::Right);
        const float ty0 = 196, ty1 = 226, bwid = 6.0f, gap = 2.83f;
        const float hi = a.peak > 100 ? float(a.peak) : 100.f;
        for (int i = 0; i < 24; i++) {
            float x = M + i * (bwid + gap);
            bool gap_ = a.hist[i] < 0;
            float h = gap_ ? 3 : a.hist[i] / hi * (ty1 - ty0);
            if (h < 3) h = 3;
            bool last = i == 23;
            c.setOpacity(last ? 255 : (t.dark ? 140 : 166));
            c.fillRoundRect(x, ty1 - h, bwid, h, 2, gap_ ? t.text3 : t.aqi[aqiBand(a.hist[i])]);
        }
        c.setOpacity(255);
        c.glow(M + 23 * (bwid + gap) + 3, ty1 - a.hist[23] / hi * (ty1 - ty0), 10, band, 120);
        dashedH(c, M, 226, ty1 - 50 / hi * (ty1 - ty0), t.text3, 140);   // Good / Satisfactory line
    }
}
