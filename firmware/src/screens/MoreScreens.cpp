#include "MoreScreens.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <ESP8266WiFi.h>
#include "../ui/Lumen.h"
#include "../services/FocusService.h"
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"
#include "../networking/NetworkManager.h"

using namespace lumen;

// Fixed texts stay in flash; copied to a stack buffer `_t` to draw.
#define FS(s) flashStr(_t, sizeof _t, PSTR(s))

static const float DEG = 0.01745329f;

static void dashLights(const Theme& t, Light* L) {
    L[0] = {214, 6, 124, t.lights[0].color, t.lights[0].strength};
    L[1] = {8, 240, 116, t.lights[1].color, t.lights[1].strength};
}
static bool minuteTick(int& lastMin) {
    time_t tt = time(nullptr);
    struct tm tm;
    localtime_r(&tt, &tm);
    if (tm.tm_min == lastMin) return false;
    lastMin = tm.tm_min;
    return true;
}
static uint16_t mix(uint16_t a, uint16_t b, float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return pack565(uint8_t(r8(a) + (r8(b) - r8(a)) * t), uint8_t(g8(a) + (g8(b) - g8(a)) * t),
                   uint8_t(b8(a) + (b8(b) - b8(a)) * t));
}
static float sw(float s) { float v = s / 9.f; return v < 1.4f ? 1.4f : v; }

// ring gauge with angular gradient + luminous tip (design2/caiden_ui.py ring_gauge)
static void ringGauge(Canvas& c, const Theme& t, float cx, float cy, float r, float thick, float frac, float a0, float sweep,
                      uint16_t c0, uint16_t c1) {
    c.setOpacity(230);
    c.arc(cx, cy, r, thick, a0, a0 + sweep, t.surfaceHi);
    c.setOpacity(255);
    frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
    if (frac > 0.005f) {
        float end = a0 + sweep * frac;
        c.arcGradient(cx, cy, r, thick, a0, end, c0, frac < 1 ? mix(c0, c1, frac) : c1);
        float rm = r - thick / 2;
        c.glow(cx + rm * cosf(end * DEG), cy + rm * sinf(end * DEG), thick * 2.6f, c1, 110);
    }
}
static void fmtMS(char* o, size_t n, uint32_t s) {
    if (s >= 3600) snprintf_P(o, n, PSTR("%lu:%02lu:%02lu"), (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else snprintf_P(o, n, PSTR("%02lu:%02lu"), (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// ---- header icons (design2/caiden_ui_ext.py)
static void iconBall(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    const float w = sw(s), r = 0.42f * s, R = r * 1.5f;
    c.ring(cx, cy, r + w / 2, w, col);
    for (float off : {0.0f, 0.16f * s}) c.arc(cx - R * 0.72f + off, cy + R * 0.72f - off, R, w * 0.7f, -62, -28, col, false);
}

// ================================================================ PRODUCTIVITY
static const Rect R_POMO(4, 34, 146, 146), R_CARDS(140, 30, 100, 124);

void ProductivityScreen::background(Canvas& c, const Theme& t) {
    Light L[3];
    dashLights(t, L);
    L[2] = {78, 112, 70, t.accent2, 40};
    paintBackground(c, t, L, 3);
}
void ProductivityScreen::enter(uint32_t now) { ver_ = g_focus.version(); lastSec_ = now; minuteTick(lastMin_); }
void ProductivityScreen::update(uint32_t now, DirtySet& d) {
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    if (g_focus.version() != ver_) { ver_ = g_focus.version(); d.addAll(); return; }
    if (now - lastSec_ >= 1000) {
        lastSec_ = now;
        if (g_focus.pomoRunning()) d.add(R_POMO);
        if (g_focus.swRunning() || g_focus.cdActive()) d.add(R_CARDS);
    }
}

void ProductivityScreen::render(Canvas& c, const Theme& t) {
    header(c, t, "Focus", nullptr, t.accent2, wifiBars());
    const FocusService& f = g_focus;
    // pomodoro ring with minute ticks
    const float cx = 78, cy = 106, r = 58;
    for (int k = 0; k < 60; k++) {
        float a = (k * 6 - 90) * DEG;
        bool major = k % 5 == 0;
        float r0 = r - (major ? 13 : 11), r1 = r - 9;
        c.setOpacity(major ? 179 : 89);
        c.line(cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1, 1.2f, t.text3);
    }
    c.setOpacity(255);
    const uint32_t left = f.pomoRemaining(), len = f.pomoLength();
    const bool brk = f.phase() == PomoPhase::Break;
    c.glow(cx, cy, r + 6, t.accent2, t.dark ? 26 : 30);
    ringGauge(c, t, cx, cy, r, 5, f.phase() == PomoPhase::Idle ? 0 : 1.f - float(left) / len, -90, 360,
              brk ? t.good : t.accent2, brk ? t.good : t.highlight);
    const char* state = f.phase() == PomoPhase::Idle ? "READY" : (brk ? "BREAK" : "FOCUS");
    label(c, int16_t(cx), int16_t(cy - 20), state, t.text3, Align::Center);
    char buf[40];
    fmtMS(buf, sizeof buf, left);
    hero(c, t, int16_t(cx), int16_t(cy + 12), buf, FONT_L_NUM_L, Align::Center);
    for (int i = 0; i < 4; i++) {   // session dots
        float x = cx - 15 + i * 10;
        int cur = f.session() - 1;
        if (f.phase() != PomoPhase::Idle && i < cur) c.fillCircle(x, cy + 26, 3, t.accent2);
        else if (f.phase() != PomoPhase::Idle && i == cur) { c.glow(x, cy + 26, 8, t.highlight, 120); c.fillCircle(x, cy + 26, 3, t.highlight); }
        else { c.setOpacity(204); c.ring(x, cy + 26, 3.2f, 1.2f, t.text3); c.setOpacity(255); }
    }
    // right column: stopwatch + countdown
    const float x0 = 144, w = 82;
    struct Card { float y; const char* lab; uint32_t val; uint16_t col; float frac; bool on; };
    const Card cards[2] = {
        {36, "ELAPSED", f.swElapsed(), t.highlight, -1, f.swRunning()},
        {96, "TIMER", f.cdRemaining(), t.warn, f.cdLength() ? float(f.cdRemaining()) / f.cdLength() : 0, f.cdActive()}};
    c.setOpacity(t.dark ? 110 : 140);
    c.fillRect(int16_t(x0 - 6), 44, 1, 112, t.stroke);             // vertical hairline beside the ring
    c.setOpacity(255);
    hairline(c, t, 96, x0, 226);
    for (const Card& k : cards) {
        label(c, int16_t(x0 + 10), int16_t(k.y + 18), k.lab, t.text3);
        c.setOpacity(k.on ? 255 : 90);
        c.fillCircle(x0 + w - 11, k.y + 14, 3, k.col);
        c.setOpacity(255);
        fmtMS(buf, sizeof buf, k.val);
        c.text(int16_t(x0 + 9), int16_t(k.y + 41), buf, FONT_L_NUM_M, k.on || k.val ? t.text : t.text3);
        if (k.frac >= 0 && k.on) {
            c.setOpacity(217);
            c.fillRoundRect(x0 + 10, k.y + 45, w - 20, 2, 1, t.surfaceHi);
            c.setOpacity(255);
            float fw = (w - 20) * k.frac;
            if (fw > 2) c.fillRoundRect(x0 + 10, k.y + 45, fw, 2, 1, k.col);
        }
    }
    // bottom: today's focus total + last 5 days
    const float by = 176;
    c.setOpacity(179);
    c.fillRect(M, int16_t(by), 212, 1, t.stroke);
    c.setOpacity(255);
    if (f.phase() != PomoPhase::Idle) snprintf_P(buf, sizeof buf, PSTR("TODAY \xC2\xB7 SESSION %u OF 4"), f.session());
    else snprintf_P(buf, sizeof buf, PSTR("TODAY"));
    label(c, M, int16_t(by + 18), buf, t.text3);
    uint16_t tm_ = f.todayMinutes();
    char hb[8], mb[8];
    snprintf_P(hb, sizeof hb, PSTR("%u"), tm_ / 60);
    snprintf_P(mb, sizeof mb, PSTR("%02u"), tm_ % 60);
    float x = M - 1;
    x += c.text(int16_t(x), int16_t(by + 48), hb, FONT_L_NUM_L, t.text) + 2;
    x += c.text(int16_t(x), int16_t(by + 48), "h", FONT_L_BODY, t.text2) + 7;
    x += c.text(int16_t(x), int16_t(by + 48), mb, FONT_L_NUM_L, t.text) + 2;
    c.text(int16_t(x), int16_t(by + 48), "m", FONT_L_BODY, t.text2);
    uint16_t mx = 60;
    for (int i = 0; i < 5; i++) if (f.dayMinutes(i) > mx) mx = f.dayMinutes(i);
    const float bx0 = 140, bw = 10, bg = 9, bh = 26, base = 224;
    for (int i = 0; i < 5; i++) {
        uint16_t v = f.dayMinutes(4 - i);   // oldest left, today right
        float hh = v ? v / float(mx) * bh : 2;
        if (hh < 2) hh = 2;
        bool today = i == 4;
        c.setOpacity(today ? 255 : (t.dark ? 115 : 140));
        c.fillRoundRect(bx0 + i * (bw + bg), base - hh, bw, hh, 3, today ? t.highlight : t.accent2);
    }
    c.setOpacity(255);
    if (tm_) c.glow(bx0 + 4 * (bw + bg) + bw / 2, base - tm_ / float(mx) * bh, 10, t.highlight, 110);
}

// ================================================================ WORLD CLOCK
// City times are computed from UTC with explicit DST rules: no setenv("TZ") (newlib leaks a
// heap block every time the TZ string grows, which killed the heap within hours).
enum class Dst : uint8_t { None, EU, US };
struct City { const char* name; int16_t stdOffset; Dst dst; };
static const City CITY[WorldScreen::CITIES] = {
    {"London", 0, Dst::EU}, {"New York", -300, Dst::US}, {"Tokyo", 540, Dst::None}, {"Dubai", 240, Dst::None}};

static int32_t daysFromCivil(int y, int m, int d) {           // Howard Hinnant's algorithm
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static int weekday(int32_t days) { return int((days % 7 + 11) % 7); }   // 0 = Sunday (1970-01-01 was Thursday)
static int32_t nthSunday(int y, int m, int n) {                          // n = 1.. ; n = 0 -> last Sunday
    if (n == 0) {
        int32_t last = daysFromCivil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - 1;
        return last - weekday(last);
    }
    int32_t first = daysFromCivil(y, m, 1);
    return first + (7 - weekday(first)) % 7 + 7 * (n - 1);
}
static int16_t cityOffset(const City& c, time_t utc) {
    if (c.dst == Dst::None) return c.stdOffset;
    struct tm g;
    gmtime_r(&utc, &g);
    const int y = g.tm_year + 1900;
    int64_t a, b;
    if (c.dst == Dst::EU) {   // last Sunday Mar 01:00 UTC -> last Sunday Oct 01:00 UTC
        a = int64_t(nthSunday(y, 3, 0)) * 86400 + 3600;
        b = int64_t(nthSunday(y, 10, 0)) * 86400 + 3600;
    } else {                  // 2nd Sunday Mar 02:00 local std -> 1st Sunday Nov 02:00 local daylight
        a = int64_t(nthSunday(y, 3, 2)) * 86400 + 2 * 3600 - c.stdOffset * 60;
        b = int64_t(nthSunday(y, 11, 1)) * 86400 + 2 * 3600 - (c.stdOffset + 60) * 60;
    }
    return int16_t(c.stdOffset + ((int64_t(utc) >= a && int64_t(utc) < b) ? 60 : 0));
}
static int16_t norm(int d) { while (d > 840) d -= 1440; while (d < -720) d += 1440; return int16_t(d); }

void WorldScreen::compute() {
    time_t now = time(nullptr);
    struct tm lt, ut;
    localtime_r(&now, &lt);
    gmtime_r(&now, &ut);
    strftime(abbr_, sizeof abbr_, "%Z", &lt);
    strftime(weekday_, sizeof weekday_, "%A", &lt);
    const int lm = lt.tm_hour * 60 + lt.tm_min, um = ut.tm_hour * 60 + ut.tm_min;
    utcDiff_ = norm(lm - um);                                   // home offset from UTC
    localMin_ = int16_t(lm);
    for (int i = 0; i < CITIES; i++) {
        const int16_t off = cityOffset(CITY[i], now);
        mins_[i] = int16_t(((um + off) % 1440 + 1440) % 1440);
        diff_[i] = norm(off - utcDiff_);
    }
}

void WorldScreen::background(Canvas& c, const Theme& t) { Light L[2]; dashLights(t, L); paintBackground(c, t, L, 2); }
void WorldScreen::enter(uint32_t now) { (void)now; minuteTick(lastMin_); compute(); }
void WorldScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    if (minuteTick(lastMin_)) { compute(); d.addAll(); }
}

static void fmtOffset(char* o, size_t n, int16_t d) {
    snprintf_P(o, n, PSTR("%c%d:%02d"), d >= 0 ? '+' : '-', abs(d) / 60, abs(d) % 60);
}

void WorldScreen::render(Canvas& c, const Theme& t) {
    header(c, t, "World clock", nullptr, t.highlight, wifiBars());
    const Config& cfg = g_config.get();
    char buf[40], city[34];
    snprintf_P(city, sizeof city, PSTR("%s"), cfg.city);
    for (char* p = city; *p; p++) *p = toupper(uint8_t(*p));
    label(c, M, 50, city, t.text2);
    char off[12];
    fmtOffset(off, sizeof off, utcDiff_);
    snprintf_P(buf, sizeof buf, PSTR("%s \xC2\xB7 UTC%s"), abbr_, off);
    label(c, 226, 50, buf, t.text3, Align::Right);
    // local hero: hh:mm (12/24 h as configured) in the clock face, weekday + weather beside it
    int h = (localMin_ / 60) % 24, m = localMin_ % 60;
    char hh[8], mm[8];
    if (cfg.h24) snprintf_P(hh, sizeof hh, PSTR("%02d"), h); else snprintf_P(hh, sizeof hh, PSTR("%d"), h % 12 ? h % 12 : 12);
    snprintf_P(mm, sizeof mm, PSTR("%02d"), m);
    const ClockGeom g = clockLayout(FONT_L_CLOCK_L, 0, 106, uint8_t(strlen(hh)));
    clock(c, t, M - 2 - g.x0, 106, hh, mm, FONT_L_CLOCK_L);
    c.text(226, 106, weekday_, FONT_L_BODY, t.text2, Align::Right);
    const WeatherData& wx = g_state.weather;
    if (wx.valid) weatherIcon(c, t, wxFromWmo(wx.code, wx.isDay), 212, 76, 26, false);
    // city rows: name + offset, 24 h day/night bar with a marker, time
    for (int i = 0; i < CITIES; i++) {
        const float y = 118 + i * 28;
        hairline(c, t, y, M, 226, i ? 90 : 140);
        const int mins = mins_[i];
        const bool night = !(390 <= mins && mins <= 1080);
        const int16_t bl = int16_t(y + 19);
        int16_t nw = c.text(M, bl, CITY[i].name, FONT_L_BODY, t.text);
        fmtOffset(off, sizeof off, diff_[i]);
        label(c, int16_t(M + nw + 6), bl, off, t.text3);
        const float bx0 = 130, bx1 = 178, byy = y + 14;
        c.setOpacity(217);
        c.fillRoundRect(bx0, byy - 2, bx1 - bx0, 4, 2, t.surfaceHi);
        c.setOpacity(140);
        c.fillRoundRect(bx0 + 6.5f / 24 * (bx1 - bx0), byy - 2, (18 - 6.5f) / 24 * (bx1 - bx0), 4, 2, t.dark ? t.sun2 : t.sun);
        c.setOpacity(255);
        const float mxp = bx0 + mins / 1440.f * (bx1 - bx0);
        const uint16_t mc = night ? t.moon : t.sun;
        c.glow(mxp, byy, 8, mc, 110);
        c.fillCircle(mxp, byy, 3.0f, (t.dark || !night) ? mc : t.accent);
        int ch = mins / 60, cm = mins % 60;
        if (cfg.h24) snprintf_P(buf, sizeof buf, PSTR("%02d:%02d"), ch, cm);
        else snprintf_P(buf, sizeof buf, PSTR("%d:%02d"), ch % 12 ? ch % 12 : 12, cm);
        c.text(226, bl, buf, FONT_L_NUM_S, night ? t.text3 : t.text, Align::Right);
    }
}

// ================================================================ CRICKET
void CricketScreen::background(Canvas& c, const Theme& t) {
    Light L[3];
    dashLights(t, L);
    L[2] = {70, 80, 70, t.accent, uint8_t(t.dark ? 34 : 36)};
    paintBackground(c, t, L, 3);
}
void CricketScreen::enter(uint32_t now) { (void)now; ver_ = g_state.bridge.version; }
void CricketScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    if (g_state.bridge.version != ver_) { ver_ = g_state.bridge.version; d.addAll(); }
}

static int oversToBalls(float o) { int whole = int(o); return whole * 6 + int(lroundf((o - whole) * 10)); }


void CricketScreen::render(Canvas& c, const Theme& t) {
    const CricketData& k = g_state.bridge.cr;
    char title[24];
    static const char* ORD[5] = {"1st", "1st", "2nd", "3rd", "4th"};
    if (!k.live) {
        header(c, t, "Cricket", nullptr, t.highlight, wifiBars());
        emptyState(c, t, iconBall, PSTR("No live match"),
                   g_state.bridge.online ? PSTR("Live scores appear here during matches") : PSTR("Cloud relay offline"));
        return;
    }
    snprintf_P(title, sizeof title, PSTR("%s \xC2\xB7 %s inn"), k.type[0] ? k.type : "Cricket", ORD[k.innings > 4 ? 4 : k.innings]);
    header(c, t, title, nullptr, t.bad, wifiBars(), "LIVE");
    const Innings& cur = k.inn[k.innings >= 2 ? 1 : 0];
    // teams line
    int16_t tw = c.text(M, 52, k.t1, FONT_L_TITLE, t.text);
    char buf[48];
    snprintf_P(buf, sizeof buf, PSTR("VS %s"), k.t2);
    label(c, int16_t(M + tw + 8), 51, buf, t.text3);
    if (k.target) { snprintf_P(buf, sizeof buf, PSTR("TARGET %d"), k.target); label(c, 226, 51, buf, t.text2, Align::Right); }
    // hero score + overs
    snprintf_P(buf, sizeof buf, PSTR("%d/%d"), cur.runs, cur.wkts);
    int16_t sw_ = hero(c, t, M - 2, 90, buf, FONT_L_NUM_L);
    const bool ovKnown = cur.overs >= 0;           // the free Cricinfo feed has no overs (-1)
    if (ovKnown) {
        snprintf_P(buf, sizeof buf, PSTR("%.1f ov"), cur.overs);
        c.text(int16_t(M + sw_ + 4), 90, buf, FONT_L_BODY, t.text2);
    }
    const int balls = ovKnown ? oversToBalls(cur.overs) : 0;
    const float crr = balls ? cur.runs * 6.f / balls : 0;
    if (k.target && k.need > 0) {
        // chase ring: runs so far / target
        ringGauge(c, t, 199, 80, 24, 4, float(cur.runs) / k.target, -90, 360, t.accent, t.highlight);
        snprintf_P(buf, sizeof buf, PSTR("%d"), k.need);
        c.text(199, 83, buf, FONT_L_NUM_S, t.text, Align::Center);
        c.text(199, 95, "NEED", FONT_L_MICRO, t.text3, Align::Center);
        // equation line ("Need 40 runs" when the feed has no balls left)
        float x = M;
        x += c.text(int16_t(x), 112, "Need ", FONT_L_BODY, t.text2);
        snprintf_P(buf, sizeof buf, PSTR("%d"), k.need);
        x += c.text(int16_t(x), 112, buf, FONT_L_BODY, t.highlight);
        if (k.balls) {
            x += c.text(int16_t(x), 112, " off ", FONT_L_BODY, t.text2);
            snprintf_P(buf, sizeof buf, PSTR("%d"), k.balls);
            x += c.text(int16_t(x), 112, buf, FONT_L_BODY, t.highlight);
            c.text(int16_t(x), 112, " balls", FONT_L_BODY, t.text2);
        } else c.text(int16_t(x), 112, " runs", FONT_L_BODY, t.text2);
        // run-rate comparison
        const float rrr = k.balls ? k.need * 6.f / k.balls : 0;
        const float rates[2] = {rrr, crr};
        const char* labs[2] = {"REQ", "CUR"};
        const uint16_t cols[2] = {t.warn, t.accent};
        float scale = rrr > crr ? rrr : crr;
        scale = scale < 12 ? 12 : scale * 1.1f;
        for (int i = 0; i < 2 && k.balls && ovKnown; i++) {
            const float y = 124 + i * 14;
            label(c, M, int16_t(y + 7), labs[i], t.text3);
            c.setOpacity(217);
            c.fillRoundRect(M + 34, y, 150, 4, 2, t.surfaceHi);
            c.setOpacity(255);
            float wv = 150 * rates[i] / scale;
            if (wv > 4) c.fillRoundRect(M + 34, y, wv, 4, 2, cols[i]);
            snprintf_P(buf, sizeof buf, PSTR("%.2f"), rates[i]);
            c.text(226, int16_t(y + 7), buf, FONT_L_LABEL, t.text, Align::Right);
        }
    } else if (ovKnown) {
        snprintf_P(buf, sizeof buf, PSTR("Run rate %.2f"), crr);
        c.text(M, 112, buf, FONT_L_BODY, t.text2);
    }
    // innings card (the free feed has no batters / ball-by-ball, so the card lists both innings)
    const float cy = 158;
    hairline(c, t, cy);
    for (int i = 0; i < 2; i++) {
        const Innings& in = k.inn[i];
        if (!in.team[0]) continue;
        const float y = cy + 16 + i * 17;
        bool batting = &in == &cur;
        if (batting) { c.glow(M + 3, y - 4, 7, t.highlight, 120); c.fillCircle(M + 3, y - 4, 2.5f, t.highlight); }
        c.text(M + 12, int16_t(y), in.team, FONT_L_BODY, batting ? t.text : t.text2);
        snprintf_P(buf, sizeof buf, PSTR("%d/%d"), in.runs, in.wkts);
        c.text(186, int16_t(y), buf, FONT_L_BODY, t.text, Align::Right);
        if (in.overs >= 0) {
            snprintf_P(buf, sizeof buf, PSTR("(%.1f)"), in.overs);
            c.text(226, int16_t(y), buf, FONT_L_BODY, t.text3, Align::Right);
        }
    }
    // status line
    char fit[80];
    hairline(c, t, 202, M, 226, 90);
    Canvas::ellipsize(k.status, FONT_L_BODY, 212, fit, sizeof fit);
    c.text(M, 220, fit, FONT_L_BODY, t.text2);
}

// ================================================================ SYSTEM
void SystemScreen::background(Canvas& c, const Theme& t) { Light L[2]; dashLights(t, L); paintBackground(c, t, L, 2); }
void SystemScreen::update(uint32_t now, DirtySet& d) {
    if (now - last_ >= 5000) { last_ = now; d.add(Rect(0, 30, 240, 210)); }   // live numbers every 5 s
}

void SystemScreen::render(Canvas& c, const Theme& t) {
    header(c, t, "System", nullptr, t.highlight, wifiBars());
    const NetState& n = g_state.net;
    const bool online = n.mode == NetMode::Online;
    char buf[48];
    // hero: signal sticks + network name + dBm
    const float hy = 36;
    const uint8_t bars = wifiBars();
    for (int i = 0; i < 4; i++) {
        float h = 8 + i * 7, x = M + i * 8;
        bool lit = i < bars + (bars ? 1 : 0) && online;   // 0..3 bars -> up to 4 sticks
        c.setOpacity(lit ? 255 : 89);
        c.fillRoundRect(x, hy + 48 - h, 5, h, 2, lit ? t.highlight : t.text3);
    }
    c.setOpacity(255);
    char fit[40];
    Canvas::ellipsize(online ? n.ssid : "Offline", FONT_L_HEAD, 176, fit, sizeof fit);
    c.text(M + 42, int16_t(hy + 22), fit, FONT_L_HEAD, t.text);
    if (online) {
        snprintf_P(buf, sizeof buf, PSTR("%d"), n.rssi);
        int16_t vw = c.text(M + 42, int16_t(hy + 46), buf, FONT_L_NUM_S, t.text2);
        c.text(int16_t(M + 42 + vw + 4), int16_t(hy + 46), "dBm", FONT_L_BODY, t.text3);
        const char* q = n.rssi > -67 ? "GOOD" : (n.rssi > -77 ? "FAIR" : "WEAK");
        label(c, 226, int16_t(hy + 45), q, n.rssi > -67 ? t.good : (n.rssi > -77 ? t.warn : t.bad), Align::Right);
    }
    // 2x2 facts on hairlines: IP, uptime, heap, settings-page password
    char ip[20], up[20], heap[20];
    snprintf_P(ip, sizeof ip, PSTR("%s"), online ? n.ip : "-");
    uint32_t s = millis() / 1000;
    if (s >= 86400) snprintf_P(up, sizeof up, PSTR("%lud %02luh"), (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24));
    else snprintf_P(up, sizeof up, PSTR("%luh %02lum"), (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    const uint32_t fh = ESP.getFreeHeap();
    snprintf_P(heap, sizeof heap, PSTR("%lu.%lu KB"), (unsigned long)(fh / 1024), (unsigned long)(fh % 1024 * 10 / 1024));
    struct Tile { const char* lab; const char* val; float frac; };
    const Tile tiles[4] = {{"IP ADDRESS", ip, -1}, {"UPTIME", up, -1}, {"FREE HEAP", heap, fh / 81920.f}, {"PASSWORD", g_net.webPassword(), -1}};   // for caiden.local
    hairline(c, t, 98);
    hairline(c, t, 148, M, 226, 90);
    for (int i = 0; i < 4; i++) {
        const float x = M + (i % 2) * 112, y = 98 + (i / 2) * 50;
        label(c, int16_t(x), int16_t(y + 20), tiles[i].lab, t.text3);
        // the password in monospace: 70 px for any 10 characters, and easy to read letter by letter
        c.text(int16_t(x), int16_t(y + 39), tiles[i].val, i == 3 ? FONT_MONO : FONT_L_TITLE, i == 3 ? t.highlight : t.text);
        if (tiles[i].frac >= 0) {
            c.setOpacity(217);
            c.fillRoundRect(x + 72, y + 15, 28, 3, 1.5f, t.surfaceHi);
            c.setOpacity(255);
            c.fillRoundRect(x + 72, y + 15, 28 * tiles[i].frac, 3, 1.5f, t.warn);
        }
    }
    hairline(c, t, 198, M, 226, 90);
    // footer: cloud relay address (green dot = data arriving) + firmware version
    label(c, 226, 220, "FW " CAIDEN_FW_VERSION, t.text3, Align::Right);
    const int16_t fwW = Canvas::textWidth("FW " CAIDEN_FW_VERSION, FONT_L_LABEL, 1);
    const bool br = g_state.bridge.online;
    const char* addr = g_config.get().relay;
    char baddr[48];
    Canvas::ellipsize(addr[0] ? addr : (br ? "Relay online" : "No relay"), FONT_L_BODY, int16_t(226 - fwW - 10 - (M + 12)),
                      baddr, sizeof baddr);
    c.glow(M + 3, 216, 8, br ? t.good : t.text3, 110);
    c.fillCircle(M + 3, 216, 3, br ? t.good : t.text3);
    c.text(M + 12, 220, baddr, FONT_L_BODY, br ? t.text2 : t.text3);
}

// ================================================================ SUN & MOON


static int hhmm(const char* s) {                       // "06:41" -> 401, -1 if unknown
    if (!s || strlen(s) < 5 || s[2] != ':' || s[0] < '0' || s[0] > '9') return -1;
    return (s[0] - '0') * 600 + (s[1] - '0') * 60 + (s[3] - '0') * 10 + (s[4] - '0');
}

static void durText(char* buf, size_t n, int mins) {
    if (mins >= 60) snprintf_P(buf, n, PSTR("%d h %d min"), mins / 60, mins % 60);
    else snprintf_P(buf, n, PSTR("%d min"), mins);
}

// Moon drawn as lit disk / shadow lens: phase f (0 new, 0.5 full), lit on the right while waxing.
static void moonDisk(Canvas& c, const Theme& t, float cx, float cy, float r, float f) {
    const float k = (1 - cosf(2 * 3.14159265f * f)) / 2;   // illuminated fraction
    const bool waxing = f < 0.5f;
    c.glow(cx, cy, r * 1.9f, t.moon, uint8_t(40 + 80 * k));
    c.setOpacity(t.dark ? 255 : 200);
    c.fillCircle(cx, cy, r, t.surfaceHi);
    c.setOpacity(255);
    if (k <= 0.5f) {
        const float d = 2 * r * k;
        c.fillCrescent(cx, cy, r, cx + (waxing ? -d : d), cy, r, t.moon);
    } else {
        c.fillCircle(cx, cy, r, t.moon);
        const float d = 2 * r * (1 - k);
        c.setOpacity(t.dark ? 255 : 200);
        c.fillCrescent(cx, cy, r, cx + (waxing ? d : -d), cy, r, t.surfaceHi);
        c.setOpacity(255);
    }
}

void SunScreen::background(Canvas& c, const Theme& t) { Light L[2]; dashLights(t, L); paintBackground(c, t, L, 2); }
void SunScreen::enter(uint32_t now) { (void)now; minuteTick(lastMin_); wver_ = g_state.weather.version; }
void SunScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    if (minuteTick(lastMin_)) d.add(Rect(0, 0, 240, 240));            // the sun moves every minute
    if (g_state.weather.version != wver_) { wver_ = g_state.weather.version; d.addAll(); }
}

void SunScreen::render(Canvas& c, const Theme& t) {
    char _t[48];
    header(c, t, "Sun & Moon", nullptr, t.sun, wifiBars());
    const WeatherData& w = g_state.weather;
    const time_t tt = time(nullptr);
    struct tm lt;
    localtime_r(&tt, &lt);
    const int nowM = lt.tm_hour * 60 + lt.tm_min;
    const int rise = hhmm(w.sunrise), set = hhmm(w.sunset);
    char buf[40];
    // --- daylight arc
    const float cx = 120, cy = 128, r = 82;
    c.setOpacity(t.dark ? 150 : 190);
    c.arc(cx, cy, r, 3, 180, 360, t.surfaceHi);
    c.setOpacity(255);
    dashedH(c, 22, 218, cy + 2, t.stroke, 160);
    const bool known = rise >= 0 && set > rise;
    const bool day = known && nowM >= rise && nowM < set;
    if (day) {
        const float p = float(nowM - rise) / float(set - rise);
        const float a = 180 + 180 * p;
        if (p > 0.005f) c.arcGradient(cx, cy, r, 3, 180, a, t.sun2, t.sun);
        const float sx = cx + cosf(a * DEG) * (r - 1.5f), sy = cy + sinf(a * DEG) * (r - 1.5f);
        c.glow(sx, sy, 22, t.sun, 170);
        c.fillCircle(sx, sy, 7, t.sun);
    }
    // centre: what is next, as a big "5h 12m"
    if (known) {
        const int left = day ? set - nowM : (nowM < rise ? rise - nowM : 1440 - nowM + rise);
        char hb[6], mb[6];
        snprintf_P(hb, sizeof hb, PSTR("%d"), left / 60);
        snprintf_P(mb, sizeof mb, PSTR("%02d"), left % 60);
        const int16_t wh = Canvas::textWidth(hb, FONT_L_NUM_L), wm = Canvas::textWidth(mb, FONT_L_NUM_L);
        const int16_t uh = Canvas::textWidth("h", FONT_L_BODY), um = Canvas::textWidth("m", FONT_L_BODY);
        float x = 120 - (wh + 2 + uh + 8 + wm + 2 + um) / 2.f;
        if (left >= 60) {
            x += hero(c, t, int16_t(x), 104, hb, FONT_L_NUM_L) + 2;
            x += c.text(int16_t(x), 104, "h", FONT_L_BODY, t.text2) + 8;
        } else {
            x += (wh + 2 + uh + 8) / 2;
        }
        x += hero(c, t, int16_t(x), 104, mb, FONT_L_NUM_L) + 2;
        c.text(int16_t(x), 104, "m", FONT_L_BODY, t.text2);
        label(c, 120, 120, day ? FS("OF DAYLIGHT LEFT") : FS("UNTIL SUNRISE"), t.text3, Align::Center);
    } else {
        c.text(120, 104, FS("Waiting for weather"), FONT_L_BODY, t.text3, Align::Center);
    }
    // sunrise / sunset / day length
    label(c, M + 4, 148, FS("SUNRISE"), t.text3);
    c.text(M + 4, 166, w.sunrise, FONT_L_TITLE, t.text);
    label(c, 222, 148, FS("SUNSET"), t.text3, Align::Right);
    c.text(222, 166, w.sunset, FONT_L_TITLE, t.text, Align::Right);
    if (known) {
        durText(buf, sizeof buf, set - rise);
        label(c, 120, 148, FS("DAY LENGTH"), t.text3, Align::Center);
        c.text(120, 166, buf, FONT_L_BODY, t.text2, Align::Center);
    }
    // --- moon card
    const float my = 180;
    hairline(c, t, my);
    const double days = (double(tt) - 947182440.0) / 86400.0;       // since new moon 2000-01-06 18:14 UTC
    double f = fmod(days / 29.530588853, 1.0);
    if (f < 0) f += 1;
    moonDisk(c, t, M + 15, my + 28, 14, float(f));
    PGM_P name = f < 0.03 || f > 0.97 ? PSTR("New moon") : f < 0.22 ? PSTR("Waxing crescent") : f < 0.28 ? PSTR("First quarter")
                     : f < 0.47 ? PSTR("Waxing gibbous") : f < 0.53 ? PSTR("Full moon") : f < 0.72 ? PSTR("Waning gibbous")
                     : f < 0.78 ? PSTR("Last quarter") : PSTR("Waning crescent");
    c.text(M + 42, int16_t(my + 25), flashStr(_t, sizeof _t, name), FONT_L_TITLE, t.text);
    const int lit = int(lround((1 - cos(2 * 3.14159265 * f)) / 2 * 100));
    const double toFull = (f < 0.5 ? 0.5 - f : 1.5 - f) * 29.530588853;
    const double toNew = (1 - f) * 29.530588853;
    if (f >= 0.47 && f < 0.53) snprintf_P(buf, sizeof buf, PSTR("%d%% lit"), lit);
    else if (f < 0.5) snprintf_P(buf, sizeof buf, PSTR("%d%% lit \xC2\xB7 full in %d d"), lit, int(lround(toFull)));
    else snprintf_P(buf, sizeof buf, PSTR("%d%% lit \xC2\xB7 new in %d d"), lit, int(lround(toNew)));
    c.text(M + 42, int16_t(my + 43), buf, FONT_L_BODY, t.text3);
}
