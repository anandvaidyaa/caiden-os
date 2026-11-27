#include "Lumen.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include "../services/ClockService.h"
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

namespace lumen {

static const float DEG = 0.01745329f;

// ---------------------------------------------------------------- weather mapping
Wx wxFromWmo(int code, bool day) {
    if (code < 0) return Wx::Unknown;
    if (code <= 1) return day ? Wx::ClearDay : Wx::ClearNight;
    if (code == 2) return day ? Wx::PartlyDay : Wx::PartlyNight;
    if (code == 3) return Wx::Cloudy;
    if (code == 45 || code == 48) return Wx::Fog;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return Wx::Rain;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Wx::Snow;
    if (code >= 95) return Wx::Storm;
    return Wx::Cloudy;
}

const char* wxWord(int code, bool day) {
    if (code < 0) return "Weather";
    if (code <= 1) return day ? "Sunny" : "Clear";
    if (code == 2) return "Partly cloudy";
    if (code == 3) return "Overcast";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    if (code >= 95) return "Storm";
    return "Cloudy";
}

// ---------------------------------------------------------------- text + icons
int16_t label(Canvas& c, int16_t x, int16_t y, const char* s, uint16_t col, Align a) {
    return c.text(x, y, s, FONT_L_LABEL, col, a, 1);
}

static float sw(float size) { float v = size / 9.f; return v < 1.4f ? 1.4f : v; }

void iconWifi(Canvas& c, const Theme& t, float cx, float cy, float size, uint8_t bars, uint16_t col, uint16_t dim) {
    (void)t;
    const float w = sw(size), by = cy + size * 0.36f;
    const float rr[3] = {size * 0.30f, size * 0.56f, size * 0.82f};
    for (int i = 0; i < 3; i++) {
        bool lit = i < bars;
        c.setOpacity(lit ? 255 : 115);
        c.arc(cx, by, rr[i] + w / 2, w, 228, 312, lit ? col : dim);
    }
    c.setOpacity(255);
    c.fillCircle(cx, by - 0.2f, w * 0.85f, col);
}

void iconBell(Canvas& c, float cx, float cy, float size, uint16_t col) {
    const float w = sw(size), r = size * 0.27f, dy = cy - size * 0.08f, rim = cy + size * 0.24f;
    c.arc(cx, dy, r + w / 2, w, 180, 360, col);
    c.line(cx - r, dy, cx - r, rim - size * 0.08f, w, col);
    c.line(cx + r, dy, cx + r, rim - size * 0.08f, w, col);
    c.line(cx - r, rim - size * 0.08f, cx - r - size * 0.09f, rim, w, col);
    c.line(cx + r, rim - size * 0.08f, cx + r + size * 0.09f, rim, w, col);
    c.line(cx - r - size * 0.09f, rim, cx + r + size * 0.09f, rim, w, col);
    c.arc(cx, rim + size * 0.04f, size * 0.11f + w / 2, w, 20, 160, col);
    c.fillCircle(cx, dy - r - w * 0.6f, w * 0.6f, col);
}

void badge(Canvas& c, float cx, float cy, int n, uint16_t col, uint16_t ring, bool hasRing) {
    char s[12];
    if (n < 10) snprintf_P(s, sizeof s, PSTR("%d"), n); else strcpy(s, "9+");
    float r = n < 10 ? 7.f : 8.5f;
    if (hasRing) c.fillCircle(cx, cy, r + 1.6f, ring);
    c.fillCircle(cx, cy, r, col);
    int lum = (2126 * r8(col) + 7152 * g8(col) + 722 * b8(col)) / 10000;
    c.text(int16_t(cx + 0.5f), int16_t(cy + 3.6f), s, FONT_L_MICRO, lum < 150 ? rgb(0xFFFFFF) : rgb(0x0A0F1E), Align::Center);
}

float eqBars(Canvas& c, float x, float cy, float h, uint16_t col, float phase, int n, float bw, float gap) {
    static const float seeds[5] = {0.55f, 1.0f, 0.7f, 0.85f, 0.45f};
    for (int i = 0; i < n && i < 5; i++) {
        float v = 0.35f + 0.65f * fabsf(sinf((phase + seeds[i]) * 3.14159265f * (1.f + 0.37f * i)));
        float bh = h * v; if (bh < bw) bh = bw;
        float xx = x + bw / 2 + i * (bw + gap);
        c.line(xx, cy + h / 2 - bw / 2, xx, cy + h / 2 - bh + bw / 2, bw, col);
    }
    return n * (bw + gap) - gap;
}

// ---------------------------------------------------------------- weather illustrations
static void cloudShape(Canvas& c, float cx, float cy, float s, uint16_t col) {
    c.fillRoundRect(cx - 0.40f * s, cy + 0.02f * s, 0.80f * s, 0.22f * s, 0.11f * s, col);
    c.fillCircle(cx - 0.21f * s, cy + 0.06f * s, 0.15f * s, col);
    c.fillCircle(cx + 0.02f * s, cy - 0.05f * s, 0.22f * s, col);
    c.fillCircle(cx + 0.23f * s, cy + 0.07f * s, 0.14f * s, col);
}

static void cloud(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col, uint16_t shade, bool sheen) {
    cloudShape(c, cx, cy + 0.04f * s, s, shade);
    cloudShape(c, cx, cy, s, col);
    if (sheen && t.dark) {
        c.setOpacity(128);
        c.fillCircle(cx - 0.02f * s, cy - 0.10f * s, 0.12f * s, rgb(0xFFFFFF));
        c.setOpacity(255);
    }
}

static void clearDay(Canvas& c, const Theme& t, float cx, float cy, float s, bool glow) {
    if (glow) {
        c.glow(cx, cy, s * 0.80f, t.sun2, t.dark ? 70 : 90);
        c.glow(cx, cy, s * 0.45f, t.sun, 90);
    }
    const float w = 0.065f * s;
    for (int k = 0; k < 8; k++) {
        float a = (k * 45 - 90) * DEG, r0 = 0.35f * s, r1 = (k % 2 == 0 ? 0.48f : 0.43f) * s;
        c.line(cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1, w, t.sun);
    }
    c.fillCircle(cx, cy, 0.25f * s, t.sun2);
    c.fillCircle(cx - 0.025f * s, cy - 0.025f * s, 0.215f * s, t.sun);
    c.setOpacity(115);
    c.fillCircle(cx - 0.08f * s, cy - 0.08f * s, 0.07f * s, rgb(0xFFFFFF));
    c.setOpacity(255);
}

static void clearNight(Canvas& c, const Theme& t, float cx, float cy, float s, bool glow) {
    if (glow) c.glow(cx, cy, s * 0.65f, t.moon, 40);
    c.fillCrescent(cx - 0.04f * s, cy + 0.02f * s, 0.28f * s, cx + 0.10f * s, cy - 0.09f * s, 0.23f * s, t.moon);
    static const float SP[2][3] = {{0.26f, 0.16f, 0.09f}, {0.14f, -0.30f, 0.06f}};
    for (auto& p : SP) {
        float x = cx + p[0] * s, y = cy + p[1] * s, l = p[2] * s;
        c.line(x - l, y, x + l, y, 0.035f * s, t.moon);
        c.line(x, y - l, x, y + l, 0.035f * s, t.moon);
    }
}

void weatherIcon(Canvas& c, const Theme& t, Wx kind, float cx, float cy, float s, bool glow, float phase) {
    switch (kind) {
        case Wx::ClearDay: clearDay(c, t, cx, cy, s, glow); break;
        case Wx::ClearNight: clearNight(c, t, cx, cy, s, glow); break;
        case Wx::PartlyDay:
            clearDay(c, t, cx - 0.14f * s, cy - 0.14f * s, 0.70f * s, glow);
            cloud(c, t, cx + 0.08f * s, cy + 0.10f * s, 0.80f * s, t.cloud, t.cloudShade, true);
            break;
        case Wx::PartlyNight:
            clearNight(c, t, cx - 0.14f * s, cy - 0.14f * s, 0.70f * s, false);
            cloud(c, t, cx + 0.08f * s, cy + 0.10f * s, 0.80f * s, t.cloud, t.cloudShade, true);
            break;
        case Wx::Cloudy:
            cloud(c, t, cx + 0.14f * s, cy - 0.10f * s, 0.62f * s, t.cloudShade, t.cloudDark, false);
            cloud(c, t, cx - 0.05f * s, cy + 0.06f * s, 0.86f * s, t.cloud, t.cloudShade, true);
            break;
        case Wx::Rain: {
            cloud(c, t, cx, cy - 0.12f * s, 0.9f * s, t.cloud, t.cloudShade, true);
            static const float D[3][2] = {{-0.20f, 0.0f}, {0.0f, 0.07f}, {0.20f, 0.0f}};
            for (int i = 0; i < 3; i++) {
                float o = fmodf(phase + i * 0.33f, 1.f) * 0.06f * s;
                float x0 = cx + D[i][0] * s, y0 = cy + 0.20f * s + D[i][1] * s + o;
                c.line(x0, y0, x0 - 0.05f * s, y0 + 0.14f * s, 0.065f * s, t.rain);
            }
            break;
        }
        case Wx::Storm: {
            cloud(c, t, cx, cy - 0.12f * s, 0.9f * s, t.cloudDark, t.cloudShade, false);
            static const float P[4][2] = {{0.06f, 0.12f}, {-0.06f, 0.29f}, {0.05f, 0.29f}, {-0.05f, 0.47f}};
            for (int i = 0; i < 3; i++)
                c.line(cx + P[i][0] * s, cy + P[i][1] * s, cx + P[i + 1][0] * s, cy + P[i + 1][1] * s, 0.07f * s, t.sun);
            c.line(cx - 0.24f * s, cy + 0.20f * s, cx - 0.28f * s, cy + 0.31f * s, 0.065f * s, t.rain);
            c.line(cx + 0.24f * s, cy + 0.20f * s, cx + 0.20f * s, cy + 0.31f * s, 0.065f * s, t.rain);
            break;
        }
        case Wx::Fog: {
            cloud(c, t, cx, cy - 0.14f * s, 0.82f * s, t.cloud, t.cloudShade, true);
            static const float B[3][3] = {{-0.36f, 0.22f, 0.18f}, {-0.22f, 0.38f, 0.29f}, {-0.30f, 0.12f, 0.40f}};
            for (int i = 0; i < 3; i++) {
                float o = sinf((phase + i * 0.3f) * 6.2832f) * 0.04f * s;
                c.line(cx + B[i][0] * s + o, cy + B[i][2] * s, cx + B[i][1] * s + o, cy + B[i][2] * s, 0.065f * s, t.fog);
            }
            break;
        }
        case Wx::Snow: {
            cloud(c, t, cx, cy - 0.12f * s, 0.9f * s, t.cloud, t.cloudShade, true);
            static const float F[3][2] = {{-0.20f, 0.27f}, {0.0f, 0.36f}, {0.20f, 0.27f}};
            for (auto& f : F) {
                float x = cx + f[0] * s, y = cy + f[1] * s, l = 0.075f * s;
                for (int k = 0; k < 3; k++) {
                    float a = (90 + k * 60) * DEG;
                    c.line(x - cosf(a) * l, y - sinf(a) * l, x + cosf(a) * l, y + sinf(a) * l, 0.045f * s,
                           t.dark ? t.cloud : t.cloudDark);
                }
            }
            break;
        }
        default:
            cloud(c, t, cx, cy, 0.8f * s, t.stroke, t.surfaceHi, false);
            break;
    }
}

// ---------------------------------------------------------------- clock
static int16_t digitCell(const Font& f) {
    Glyph g; int16_t mx = 0;
    for (char d = '0'; d <= '9'; d++) if (fontGlyph(f, d, g) && g.adv > mx) mx = g.adv;
    return int16_t(mx - lroundf(f.px * 0.04f));
}

ClockGeom clockLayout(const Font& f, float x, int16_t baseline, uint8_t hourDigits) {
    ClockGeom g;
    const int16_t cw = digitCell(f), cg = int16_t(lroundf(f.px * 0.30f));
    const float total = (hourDigits + 2) * cw + cg;
    g.x0 = x - float(int(total) / 2);   // integer centring = reference (total // 2)
    g.x1 = g.x0 + total;
    Glyph z; int16_t cap = fontGlyph(f, '0', z) ? -z.y0 : f.asc;
    g.capTop = baseline - cap;
    g.colonX = g.x0 + hourDigits * cw + cg / 2.f;
    const float mid = baseline - cap * 0.52f;
    g.colonY0 = mid - cap * 0.19f;
    g.colonY1 = mid + cap * 0.19f;
    g.colonR = f.px * 0.042f;
    return g;
}

void clock(Canvas& c, const Theme& t, float x, int16_t baseline, const char* hh, const char* mm, const Font& f,
           const uint8_t* digitAlpha, uint8_t colonAlpha) {
    const uint8_t hd = uint8_t(strlen(hh));
    const ClockGeom g = clockLayout(f, x, baseline, hd);
    const int16_t cw = digitCell(f), cg = int16_t(lroundf(f.px * 0.30f));
    char d[2] = {0, 0};
    uint8_t i = 0;
    for (uint8_t k = 0; k < hd; k++, i++) {
        d[0] = hh[k];
        c.setOpacity(digitAlpha ? digitAlpha[i] : 255);
        c.textGradient(int16_t(lroundf(g.x0 + cw * k + cw / 2.f)), baseline, d, f, t.clockTop, t.clockBot, Align::Center);
    }
    for (uint8_t k = 0; k < 2; k++, i++) {
        d[0] = mm[k];
        c.setOpacity(digitAlpha ? digitAlpha[i] : 255);
        c.textGradient(int16_t(lroundf(g.x0 + cw * hd + cg + cw * k + cw / 2.f)), baseline, d, f, t.clockTop,
                       t.clockBot, Align::Center);
    }
    c.setOpacity(colonAlpha);
    c.fillCircle(g.colonX, g.colonY0, g.colonR, t.colon);
    c.fillCircle(g.colonX, g.colonY1, g.colonR, t.colon);
    c.setOpacity(255);
}

// ---------------------------------------------------------------- dashboards
uint8_t wifiBars() {
    if (g_state.net.mode != NetMode::Online) return 0;
    int r = g_state.net.rssi;
    return r > -60 ? 3 : r > -72 ? 2 : r > -84 ? 1 : 0;
}

const char* compass(uint16_t deg) {
    static const char* D[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return D[((deg % 360) + 22) / 45 % 8];
}

bool g_headerDrawn = false;

bool musicPlaying() {
    const BridgeData& b = g_state.bridge;
    return b.online && b.sp.on && b.sp.playing;
}

void header(Canvas& c, const Theme& t, const char* title, HeaderIcon icon, uint16_t iconCol, uint8_t bars,
            const char* live, uint32_t dataAt) {
    // CAIDEN OS 3 whisper line: no icon or Wi-Fi glyph, just a coloured dot (the old icon colour),
    // the page label and the time. Offline: the time turns quiet and a red dot sits beside it.
    // Laid out right to left: time (or LIVE, or the data's age), then the music and mining badges,
    // then the title, shortened to the room that is left. Nothing overlaps.
    (void)icon;
    g_headerDrawn = true;
    int16_t rx;                                   // left edge of the right-hand block
    if (live) {                                   // e.g. "LIVE" with a red dot instead of the time
        int16_t w = c.text(W - M, 21, live, FONT_L_LABEL, t.text, Align::Right, 1);
        c.glow(W - M - w - 8, 17, 8, t.bad, 120);
        c.fillCircle(W - M - w - 8, 17, 3, t.bad);
        rx = W - M - w - 12;
    } else {
        const time_t now = time(nullptr);
        if (dataAt && now > time_t(dataAt) + 1800) {   // stale (relay away): show the data's age
            const uint32_t m = uint32_t(now - dataAt) / 60;
            char age[16];
            if (m < 60) snprintf_P(age, sizeof age, PSTR("%uM AGO"), unsigned(m));
            else if (m < 48 * 60) snprintf_P(age, sizeof age, PSTR("%uH AGO"), unsigned(m / 60));
            else snprintf_P(age, sizeof age, PSTR("%uD AGO"), unsigned(m / 1440));
            int16_t w = c.text(W - M, 21, age, FONT_L_LABEL, t.warn, Align::Right, 1);
            c.fillCircle(W - M - w - 7, 17, 2.4f, t.warn);
            rx = W - M - w - 10;
        } else {
            char hh[4], mm[4], tbuf[10];
            bool am;
            g_clock.hhmm(hh, mm, g_config.get().h24, am);   // follows the 12/24 h setting, like home
            snprintf_P(tbuf, sizeof tbuf, PSTR("%s:%s"), hh, mm);
            int16_t w = c.text(W - M, 22, tbuf, FONT_L_NUM_S, bars ? t.text2 : t.text3, Align::Right);
            if (!bars) c.fillCircle(W - M - w - 7, 17, 2.4f, t.bad);
            rx = W - M - w - (bars ? 0 : 10);
        }
    }
    // badges, right to left, 8 px apart; the phase comes from the clock so any redraw matches
    float x = rx - 8;
    if (musicPlaying()) {
        x -= 17;
        eqBars(c, x, 16, 11, t.highlight, (millis() % 2400) / 2400.f);
        x -= 8;
    }
    if (miningShown()) {
        x -= 12;
        mineBadge(c, t, x, 16, miningActive(), (millis() % 1000) / 1000.f);
        x -= 8;
    }
    c.glow(M + 2, 17, 7, iconCol, 90);
    c.fillCircle(M + 2, 17, 2.4f, iconCol);
    char up[40], fit[40];
    snprintf_P(up, sizeof up, PSTR("%s"), title);
    for (char* p = up; *p; p++) *p = toupper(uint8_t(*p));
    Canvas::ellipsize(up, FONT_L_LABEL, int16_t(x - (M + 11)), fit, sizeof fit);
    label(c, M + 11, 21, fit, t.text2);
}

bool miningShown() { return g_config.get().mineOn; }
bool miningActive() { return g_config.get().mineOn && g_state.mine.status == MineStatus::Mining; }

void mineBadge(Canvas& c, const Theme& t, float x, float cy, bool active, float phase) {
    // a pickaxe pivoting at its handle end (bottom left), tapping the ground; a spark on each tap
    const float DEG = 0.01745329f;
    float ang = -48;                                          // resting angle (degrees, -90 = up)
    if (active) ang = phase < 0.7f ? -72 + 36 * (phase / 0.7f) * (phase / 0.7f) : -36;
    const float px = x + 2, py = cy + 5, len = 11;
    const float dx = cosf(ang * DEG), dy = sinf(ang * DEG), tx = px + dx * len, ty = py + dy * len;
    const uint16_t col = active ? t.sun : t.text3;
    c.line(px, py, tx, ty, 1.6f, col);                         // handle
    c.line(tx - dy * 4.5f - dx * 1.5f, ty + dx * 4.5f - dy * 1.5f, tx + dy * 4.5f, ty - dx * 4.5f, 2.0f, col);   // head
    if (active && phase >= 0.7f) {
        const float k = (phase - 0.7f) / 0.3f;
        c.setOpacity(uint8_t(255 * (1 - k)));
        c.fillCircle(tx + dy * 4.5f + 2 + 3 * k, ty - dx * 4.5f - 2 * k, 1.2f, rgb(0xFFF6D8));
        c.setOpacity(255);
    }
}

void segments(Canvas& c, const Theme& t, uint8_t n, uint8_t i, float frac, float y, float x0, float x1) {
    if (!n) return;
    const float gap = n > 10 ? 3 : 4, w = (x1 - x0 - gap * (n - 1)) / n;
    frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
    for (uint8_t k = 0; k < n; k++) {
        const float x = x0 + k * (w + gap);
        c.setOpacity(t.dark ? 140 : 180);
        c.fillRoundRect(x, y, w, 2, 1, t.surfaceHi);
        c.setOpacity(255);
        const float f = k < i ? 1 : (k == i ? frac : 0);
        if (f * w >= 1) c.fillRoundRect(x, y, w * f, 2, 1, t.dark ? t.text : t.accent);
    }
}

void hairline(Canvas& c, const Theme& t, float y, float x0, float x1, uint8_t alpha) {
    c.setOpacity(alpha);
    c.fillRect(int16_t(x0), int16_t(y), int16_t(x1 - x0), 1, t.stroke);
    c.setOpacity(255);
}

void degree(Canvas& c, float x, float y, float r, uint16_t col) { c.ring(x, y, r, r * 0.38f < 1.6f ? 1.6f : r * 0.38f, col); }

int16_t hero(Canvas& c, const Theme& t, int16_t x, int16_t y, const char* s, const Font& f, Align a) {
    return c.textGradient(x, y, s, f, t.clockTop, t.clockBot, a);
}

const char* flashStr(char* b, size_t n, PGM_P p) { strncpy_P(b, p, n - 1); b[n - 1] = 0; return b; }

void emptyState(Canvas& c, const Theme& t, HeaderIcon icon, PGM_P title, PGM_P hint) {
    char buf[48];
    c.glow(120, 98, 58, t.accent, t.dark ? 60 : 50);
    c.setOpacity(t.dark ? 70 : 110);
    c.ring(120, 98, 44, 1, t.stroke);
    c.setOpacity(t.dark ? 150 : 190);
    c.ring(120, 98, 30, 1, t.stroke);
    c.setOpacity(255);
    icon(c, t, 120, 98, 30, t.highlight);
    c.text(120, 166, flashStr(buf, sizeof buf, title), FONT_L_HEAD, t.text, Align::Center);
    c.text(120, 188, flashStr(buf, sizeof buf, hint), FONT_L_BODY, t.text3, Align::Center);
}

void glassCard(Canvas& c, const Theme& t, float x, float y, float w, float h, float r) {
    c.setOpacity(t.glassA);
    c.fillRoundRect(x, y, w, h, r, t.surface);
    c.setOpacity(t.dark ? 230 : 255);
    c.strokeRoundRect(x, y, w, h, r, 1, t.stroke);
    // bright top edge (light from above): the stroke clipped to the top 70 % of the radius
    Rect save = c.clip();
    bool had = c.hasClip();
    Rect top(int16_t(x), int16_t(y), int16_t(w), int16_t(r * 0.7f));
    c.setClip(had ? top.intersect(save) : top);
    c.setOpacity(t.glassTopA);
    c.strokeRoundRect(x, y, w, h, r, 1, t.glassTop);
    if (had) c.setClip(save); else c.clearClip();
    c.setOpacity(255);
}

void iconPin(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col) {
    const float R = size * 0.32f, top = cy - size * 0.12f;
    for (int i = 0; i < 8; i++) {
        float k = i / 7.f, rr = R * powf(1 - k, 1.3f);
        c.fillCircle(cx, top + k * size * 0.46f, rr < 0.5f ? 0.5f : rr, col);
    }
    c.fillCircle(cx, top, R * 0.42f, t.bg);
}

void iconLeaf(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col) {
    const float w = sw(size), L = size * 0.78f, R = size * 0.25f, ux = 0.7071f, uy = -0.7071f;
    const float x0 = cx - ux * L * 0.42f, y0 = cy - uy * L * 0.42f;
    for (int i = 0; i < 11; i++) {
        float k = (i + 0.5f) / 11.f, rr = R * powf(sinf(3.14159265f * k), 0.8f);
        c.fillCircle(x0 + ux * L * k, y0 + uy * L * k, rr < 0.6f ? 0.6f : rr, col);
    }
    c.line(x0 - ux * 2.2f, y0 - uy * 2.2f, x0 + ux * L * 0.55f, y0 + uy * L * 0.55f, 1.1f, t.bg);
    c.line(x0 - ux * 3.0f, y0 - uy * 3.0f, x0, y0, w * 0.9f, col);
}

void iconDroplet(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col) {
    (void)t;
    const float R = size * 0.30f, by = cy + size * 0.16f;
    for (int i = 0; i < 9; i++) {
        float k = i / 8.f, rr = R * powf(1 - k, 1.25f);
        c.fillCircle(cx, by - k * size * 0.46f, rr < 0.6f ? 0.6f : rr, col);
    }
}

void iconWind(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col) {
    (void)t;
    const float w = sw(size), x0 = cx - size * 0.42f;
    static const float L[3][3] = {{-0.22f, 0.62f, 0.13f}, {0.02f, 0.78f, 0.0f}, {0.26f, 0.50f, 0.12f}};
    for (auto& l : L) {
        float y = cy + l[0] * size, xe = x0 + l[1] * size;
        c.line(x0, y, xe, y, w, col);
        if (l[2] > 0) {
            float rr = l[2] * size;
            if (l[0] < 0) c.arc(xe, y - rr, rr + w / 2, w, -150, 90, col);
            else c.arc(xe, y + rr, rr + w / 2, w, -90, 150, col);
        }
    }
}

void iconUV(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col) {
    (void)t;
    const float w = sw(size);
    c.ring(cx, cy, size * 0.20f + w / 2, w, col);
    for (int k = 0; k < 8; k++) {
        float a = k * 45 * DEG;
        c.line(cx + cosf(a) * size * 0.36f, cy + sinf(a) * size * 0.36f, cx + cosf(a) * size * 0.46f,
               cy + sinf(a) * size * 0.46f, w, col);
    }
}

void iconUmbrella(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col) {
    (void)t;
    const float w = sw(size);
    c.arc(cx, cy, 0.46f * size, 0.46f * size, 180, 360, col, false);     // filled canopy
    c.line(cx, cy, cx, cy + 0.36f * size, w, col);
    c.arc(cx - 0.11f * size, cy + 0.36f * size, 0.11f * size + w / 2, w, 0, 180, col);
}

void dashedH(Canvas& c, float x0, float x1, float y, uint16_t col, uint8_t alpha, int dash, int gap) {
    c.setOpacity(alpha);
    for (int x = int(x0); x < int(x1); x += dash + gap) c.fillRect(int16_t(x), int16_t(y), int16_t(dash), 1, col);
    c.setOpacity(255);
}

void dashedV(Canvas& c, float x, float y0, float y1, uint16_t col, uint8_t alpha, int dash, int gap) {
    c.setOpacity(alpha);
    for (int y = int(y0); y < int(y1); y += dash + gap) c.fillRect(int16_t(x), int16_t(y), 1, int16_t(dash), col);
    c.setOpacity(255);
}

}  // namespace lumen
