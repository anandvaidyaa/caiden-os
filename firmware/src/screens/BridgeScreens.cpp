#include "BridgeScreens.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include "../ui/Lumen.h"
#include "../services/BridgeService.h"
#include "../services/ClockService.h"
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

using namespace lumen;

// Fixed texts stay in flash; copied to a stack buffer `_t` to draw.
#define FS(s) flashStr(_t, sizeof _t, PSTR(s))

static const float DEG = 0.01745329f;
static const uint16_t SPOTIFY_GREEN = rgb(0x1ED760);

static uint16_t mix(uint16_t a, uint16_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return pack565(uint8_t(r8(a) + (r8(b) - r8(a)) * t), uint8_t(g8(a) + (g8(b) - g8(a)) * t),
                   uint8_t(b8(a) + (b8(b) - b8(a)) * t));
}

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

// ---- small components (design2/caiden_ui_ext.py / caiden_ui.py)
static void iconTrend(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    const float w = s / 9.f < 1.4f ? 1.4f : s / 9.f;
    static const float P[4][2] = {{-0.45f, 0.30f}, {-0.12f, -0.02f}, {0.08f, 0.16f}, {0.42f, -0.22f}};
    for (int i = 0; i < 3; i++) c.line(cx + P[i][0] * s, cy + P[i][1] * s, cx + P[i + 1][0] * s, cy + P[i + 1][1] * s, w, col);
    float tx = cx + 0.42f * s, ty = cy - 0.22f * s;
    c.line(tx, ty, tx - 0.24f * s, ty, w, col);
    c.line(tx, ty, tx, ty + 0.24f * s, w, col);
}
static void iconNews(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    const float w = s / 9.f < 1.4f ? 1.4f : s / 9.f;
    c.strokeRoundRect(cx - 0.44f * s, cy - 0.38f * s, 0.88f * s, 0.80f * s, 0.14f * s, w, col);
    c.fillRect(int16_t(cx - 0.26f * s), int16_t(cy - 0.20f * s), int16_t(0.22f * s + 0.5f), int16_t(0.20f * s + 0.5f), col);
    c.line(cx + 0.06f * s, cy - 0.16f * s, cx + 0.26f * s, cy - 0.16f * s, w * 0.8f, col);
    c.line(cx + 0.06f * s, cy - 0.02f * s, cx + 0.26f * s, cy - 0.02f * s, w * 0.8f, col);
    c.line(cx - 0.26f * s, cy + 0.18f * s, cx + 0.26f * s, cy + 0.18f * s, w * 0.8f, col);
}
static void iconCalendar(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    const float w = s / 9.f < 1.4f ? 1.4f : s / 9.f;
    c.strokeRoundRect(cx - 0.44f * s, cy - 0.32f * s, 0.88f * s, 0.76f * s, 0.16f * s, w, col);
    c.line(cx - 0.40f * s, cy - 0.06f * s, cx + 0.40f * s, cy - 0.06f * s, w * 0.8f, col);
    for (float dx : {-0.2f, 0.2f}) c.line(cx + dx * s, cy - 0.46f * s, cx + dx * s, cy - 0.24f * s, w, col);
    c.fillCircle(cx + 0.16f * s, cy + 0.20f * s, w * 0.9f, col);
}
static void tri(Canvas& c, float cx, float cy, bool up, float size, uint16_t col) {
    int n = int(size);
    for (int i = 0; i < n; i++) {
        float k = n > 1 ? float(i) / (n - 1) : 0, y = up ? cy - size / 2 + k * size : cy + size / 2 - k * size;
        float hw = k * size * 0.55f;
        c.line(cx - hw, y, cx + hw, y, 1.2f, col);
    }
}
// Catmull-Rom through n points (x evenly spaced), `sub` steps per segment. Returns the point count.
static int catmull(const float* xs, const float* ys, int n, int sub, float* ox, float* oy, int cap) {
    int m = 0;
    for (int i = 0; i + 1 < n && m < cap; i++) {
        int i0 = i ? i - 1 : 0, i3 = i + 2 < n ? i + 2 : n - 1;
        for (int k = 0; k < sub && m < cap; k++) {
            float s = float(k) / sub, s2 = s * s, s3 = s2 * s;
            ox[m] = 0.5f * (2 * xs[i] + (-xs[i0] + xs[i + 1]) * s + (2 * xs[i0] - 5 * xs[i] + 4 * xs[i + 1] - xs[i3]) * s2 +
                            (-xs[i0] + 3 * xs[i] - 3 * xs[i + 1] + xs[i3]) * s3);
            oy[m] = 0.5f * (2 * ys[i] + (-ys[i0] + ys[i + 1]) * s + (2 * ys[i0] - 5 * ys[i] + 4 * ys[i + 1] - ys[i3]) * s2 +
                            (-ys[i0] + 3 * ys[i] - 3 * ys[i + 1] + ys[i3]) * s3);
            m++;
        }
    }
    if (m < cap && n > 0) { ox[m] = xs[n - 1]; oy[m] = ys[n - 1]; m++; }
    return m;
}

static void fmtTime(char* out, size_t n, uint32_t ms) {
    uint32_t s = ms / 1000;
    snprintf(out, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// ================================================================ SPOTIFY
static bool artSrc(void* ctx, int16_t row, uint16_t* out, int16_t x0, int16_t w) {
    (void)ctx;
    return g_bridge.artRow(row, out, x0, w);
}

static uint32_t spProgress() {
    const SpotifyData& sp = g_state.bridge.sp;
    uint32_t p = sp.progressMs + (sp.playing ? millis() - sp.atMs : 0);
    return sp.durationMs && p > sp.durationMs ? sp.durationMs : p;
}

static const Rect R_SP_PROGRESS(0, 194, 240, 34), R_SP_EQ(102, 206, 36, 18);

void SpotifyScreen::background(Canvas& c, const Theme& t) {
    static Light L[5];
    const SpotifyData& sp = g_state.bridge.sp;
    uint16_t c1 = sp.c1 ? sp.c1 : t.accent2, c2 = sp.c2 ? sp.c2 : t.highlight;
    L[0] = {120, 92, 128, c1, uint8_t(t.dark ? 95 : 70)};
    L[1] = {196, 150, 90, c2, uint8_t(t.dark ? 60 : 50)};
    L[2] = {10, 240, 100, t.lights[1].color, t.lights[1].strength};
    // artwork halo + under-glow (previously per-pixel glows on every frame, ~40k px)
    L[3] = {120, 90, 104, c1, uint8_t(t.dark ? 105 : 75)};
    L[4] = {120, 156, 60, c2, uint8_t(t.dark ? 100 : 70)};
    paintBackground(c, t, L, 5);
}

void SpotifyScreen::enter(uint32_t now) { ver_ = g_state.bridge.spVersion; lastSec_ = now; lastEq_ = now; }

void SpotifyScreen::update(uint32_t now, DirtySet& d) {
    const SpotifyData& sp = g_state.bridge.sp;
    static int lastMin = -1;
    if (minuteTick(lastMin)) d.add(HEADER_TIME_RECT);
    if (g_state.bridge.spVersion != ver_) { ver_ = g_state.bridge.spVersion; d.addAll(); return; }
    if (sp.playing && now - lastSec_ >= 1000) { lastSec_ = now; d.add(R_SP_PROGRESS); }
    if (sp.playing && now - lastEq_ >= 83) {      // equaliser ~12 fps
        lastEq_ = now;
        eqPhase_ = (now % 2400) / 2400.f;
        d.add(R_SP_EQ);
    }
}


static void iconNote(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    c.fillCircle(cx - 0.18f * s, cy + 0.22f * s, 0.13f * s, col);
    c.fillCircle(cx + 0.22f * s, cy + 0.14f * s, 0.13f * s, col);
    c.line(cx - 0.07f * s, cy + 0.22f * s, cx - 0.07f * s, cy - 0.30f * s, 0.07f * s, col);
    c.line(cx + 0.33f * s, cy + 0.14f * s, cx + 0.33f * s, cy - 0.38f * s, 0.07f * s, col);
    c.line(cx - 0.07f * s, cy - 0.30f * s, cx + 0.33f * s, cy - 0.38f * s, 0.10f * s, col);
}

void SpotifyScreen::render(Canvas& c, const Theme& t) {
    if (!g_state.bridge.sp.on) {
        header(c, t, "Spotify", nullptr, t.text3, wifiBars());
        emptyState(c, t, iconNote, PSTR("Nothing playing"),
                   !g_state.bridge.online ? PSTR("Cloud relay offline") : g_state.bridge.spSetup ? PSTR("Play music on Spotify to see it here") : PSTR("Sign in to Spotify on the relay"));
        return;
    }
    const SpotifyData& sp = g_state.bridge.sp;
    header(c, t, "Spotify", nullptr, sp.playing ? SPOTIFY_GREEN : t.text3, wifiBars());
    const uint16_t c1 = sp.c1 ? sp.c1 : t.accent2, c2 = sp.c2 ? sp.c2 : t.highlight;
    const float ax = 60, ay = 34, A = 120;
    (void)c2;   // halo + under-glow are part of the cached background now
    if (sp.artReady[0] && strcmp(sp.artReady, sp.artId) == 0) {
        c.image(int16_t(ax), int16_t(ay), int16_t(A), int16_t(A), artSrc, nullptr, 14);
    } else {   // artwork still loading: quiet placeholder with a note
        c.fillRoundRect(ax, ay, A, A, 14, t.surfaceHi);
        c.line(120 - 6, 80, 120 - 6, 104, 2.2f, t.text3);
        c.line(120 - 6, 80, 120 + 10, 76, 2.2f, t.text3);
        c.line(120 + 10, 76, 120 + 10, 100, 2.2f, t.text3);
        c.fillCircle(120 - 10, 104, 4.5f, t.text3);
        c.fillCircle(120 + 6, 100, 4.5f, t.text3);
    }
    c.setOpacity(t.dark ? 56 : 89);
    c.strokeRoundRect(ax, ay, A, A, 14, 1, t.dark ? rgb(0xFFFFFF) : t.stroke);
    c.setOpacity(255);

    char fit[72];
    Canvas::ellipsize(sp.title[0] ? sp.title : "Nothing playing", FONT_L_TITLE, 212, fit, sizeof fit);
    c.text(120, 175, fit, FONT_L_TITLE, t.text, Align::Center);
    Canvas::ellipsize(sp.artist, FONT_L_BODY, 212, fit, sizeof fit);
    c.text(120, 192, fit, FONT_L_BODY, t.text2, Align::Center);

    // progress: track, elapsed fill (artwork colour -> highlight, 4 capsules), tip
    const float py = 203, pw = 212;
    const uint32_t p = spProgress();
    const float frac = sp.durationMs ? float(p) / sp.durationMs : 0;
    c.setOpacity(217);
    c.fillRoundRect(M, py, pw, 4, 2, t.surfaceHi);
    c.setOpacity(255);
    const float fx = M + pw * frac, L = fx - M;
    if (L > 1) {
        for (int k = 0; k < 4; k++) {
            float x = M + L * k / 4;
            c.fillRoundRect(k ? x - 2 : x, py, L / 4 + (k ? 2 : 0), 4, 2, mix(c1, t.highlight, (k + 0.5f) / 4));
        }
    }
    c.glow(fx, py + 2, 9, t.highlight, 120);
    c.fillCircle(fx, py + 2, 4.0f, t.text);
    char tb[12];
    fmtTime(tb, sizeof tb, p);
    c.text(M, 222, tb, FONT_L_LABEL, t.text2, Align::Left, 1);
    fmtTime(tb, sizeof tb, sp.durationMs);
    c.text(226, 222, tb, FONT_L_LABEL, t.text3, Align::Right, 1);
    if (sp.playing) eqBars(c, 120 - 13, 216, 12, t.highlight, eqPhase_, 5, 2.6f, 2.4f);
    else label(c, 120, 222, "PAUSED", t.text3, Align::Center);
}

// ================================================================ MARKETS
// CAIDEN OS 3 (design2/render_v3.py v3_markets): green/red wash for the day, big index value with
// small decimals, full-bleed intraday chart, quiet rows on hairlines.
void MarketsScreen::background(Canvas& c, const Theme& t) {
    static Light L[3];
    const BridgeData& b = g_state.bridge;
    const uint16_t col = b.markets && b.mk[0].change < 0 ? t.down : t.up;
    L[0] = {40, 150, 150, col, uint8_t(t.dark ? 34 : 40)};
    L[1] = {220, 0, 110, t.lights[0].color, t.lights[0].strength};
    L[2] = {8, 240, 90, t.lights[1].color, uint8_t(t.lights[1].strength / 2)};
    paintBackground(c, t, L, 3);
}
void MarketsScreen::enter(uint32_t now) { (void)now; ver_ = g_state.bridge.version; minuteTick(lastMin_); }
void MarketsScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    if (g_state.bridge.version != ver_) { ver_ = g_state.bridge.version; d.addAll(); }
}

static bool numericOnly(const char* s) {
    for (; *s; s++) if (!strchr("0123456789.,-+% ", *s)) return false;
    return true;
}

void MarketsScreen::render(Canvas& c, const Theme& t) {
    const BridgeData& b = g_state.bridge;
    if (!b.markets) {
        header(c, t, "Markets", nullptr, t.highlight, wifiBars());
        emptyState(c, t, iconTrend, PSTR("Waiting for markets"),
                   b.online ? PSTR("Prices appear after the next update") : PSTR("Cloud relay offline"));
        return;
    }
    const MarketRow& h = b.mk[0];
    const bool up = h.change >= 0;
    const uint16_t col = up ? t.up : t.down;
    header(c, t, h.name, nullptr, col, wifiBars(), nullptr, b.mkAt);

    // hero value: integer part large, decimals small
    char buf[24], ip[16];
    snprintf(ip, sizeof ip, "%s", h.value);
    char* dot = strchr(ip, '.');
    if (numericOnly(h.value)) {
        if (dot) *dot = 0;
        int16_t w = hero(c, t, M - 2, 86, ip, FONT_L_NUM_L);
        if (dot) c.text(int16_t(M - 2 + w + 2), 86, h.value + (dot - ip), FONT_L_NUM_S, t.text2);
    } else {
        c.text(M - 1, 86, h.value, FONT_L_HEAD, t.text);     // value with a currency sign / unit
    }
    // change: absolute in colour, then a tinted chip with arrow + percent
    snprintf(buf, sizeof buf, "%+.2f", h.delta);
    int16_t dw = c.text(M, 108, buf, FONT_L_BODY, col);
    snprintf(buf, sizeof buf, "%+.2f%%", h.change);
    const float chipX = M + dw + 10, chipW = Canvas::textWidth(buf, FONT_L_BODY) + 26;
    c.setOpacity(t.dark ? 46 : 31);
    c.fillRoundRect(chipX, 95, chipW, 19, 9.5f, col);
    c.setOpacity(255);
    tri(c, chipX + 10, 104.5f, up, 5.5f, col);
    c.text(int16_t(chipX + chipW - 8), 109, buf, FONT_L_BODY, col, Align::Right);
    if (h.prev[0]) {
        snprintf(buf, sizeof buf, "PREV %s", h.prev);
        label(c, 226, 108, buf, t.text3, Align::Right);
    }

    // full-bleed intraday chart, edge to edge, fading area, glowing tip
    const float x0 = 0, x1 = 226, y0 = 124, y1 = 160;
    const Rect win = c.window();
    if (h.sparkN >= 2 && win.y < 170 && win.b() > 116) {   // only bands that touch the chart do the maths
        float xs[24], ys[24], cx_[72], cy_[72];
        for (int i = 0; i < h.sparkN; i++) {
            xs[i] = x0 + i * (x1 - x0) / (h.sparkN - 1);
            ys[i] = y1 - h.spark[i] / 255.f * (y1 - y0);
        }
        const int n = catmull(xs, ys, h.sparkN, 3, cx_, cy_, 72);
        for (int i = 0; i + 1 < n; i++) {
            for (float sx = cx_[i]; sx < cx_[i + 1]; sx += 3) {
                float k = (sx + 1.5f - cx_[i]) / (cx_[i + 1] - cx_[i] + 1e-3f), sy = cy_[i] + (cy_[i + 1] - cy_[i]) * k;
                float hh = y1 + 6 - sy;
                if (hh <= 0) continue;
                c.setOpacity(t.dark ? 51 : 41);
                c.fillRect(int16_t(sx), int16_t(sy), 3, int16_t(hh * 0.5f), col);
                c.setOpacity(t.dark ? 22 : 18);
                c.fillRect(int16_t(sx), int16_t(sy + hh * 0.5f), 3, int16_t(hh * 0.5f + 1), col);
            }
        }
        c.setOpacity(255);
        if (h.prevPos >= 0) dashedH(c, 0, 240, y1 - h.prevPos / 255.f * (y1 - y0), t.text3, 115);
        for (int i = 0; i + 1 < n; i++) c.line(cx_[i], cy_[i], cx_[i + 1], cy_[i + 1], 2.2f, col);
        c.glow(cx_[n - 1], cy_[n - 1], 14, col, 170);
        c.fillCircle(cx_[n - 1], cy_[n - 1], 3.2f, t.text);
    }

    // rows: dot, name, value, change. Hairlines, no box.
    for (int i = 1; i < b.markets && i < 4; i++) {
        const MarketRow& m = b.mk[i];
        const int16_t y = int16_t(188 + (i - 1) * 19);
        if (i > 1) hairline(c, t, y - 13, M, 226, 90);
        const uint16_t rc = m.change >= 0 ? t.up : t.down;
        c.fillCircle(M + 2, y - 4, 2.4f, rc);
        char nm[16];
        Canvas::ellipsize(m.name, FONT_L_LABEL, 80, nm, sizeof nm);
        label(c, M + 10, y, nm, t.text2);
        c.text(162, y, m.value, FONT_L_BODY, t.text, Align::Right);
        snprintf(buf, sizeof buf, "%+.2f%%", m.change);
        c.text(226, y, buf, FONT_L_BODY, rc, Align::Right);
    }
}

// ================================================================ NEWS
static uint32_t storyMs() { return uint32_t(g_config.get().storySec) * 1000UL; }   // portal: Pages tab

// CAIDEN OS 3 (design2/render_v3.py v3_news): category glow, story segments, source line, the
// headline large and left aligned (Outfit 23), quiet "up next" footer.
void NewsScreen::background(Canvas& c, const Theme& t) {
    static Light L[2];
    L[0] = {0, 0, 150, t.accent2, uint8_t(t.dark ? 55 : 45)};
    L[1] = {240, 240, 120, t.lights[1].color, t.lights[1].strength};
    paintBackground(c, t, L, 2);
}
void NewsScreen::enter(uint32_t now) { idx_ = 0; storyAt_ = now; lastTick_ = now; minuteTick(lastMin_); ver_ = g_state.bridge.version; }

void NewsScreen::update(uint32_t now, DirtySet& d) {
    const BridgeData& b = g_state.bridge;
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    if (b.version != ver_) { ver_ = b.version; d.addAll(); return; }   // new headline list
    if (!b.news) return;
    if (now - storyAt_ >= storyMs()) { marquee_ = false; storyAt_ = now; idx_ = uint8_t((idx_ + 1) % b.news); d.addAll(); return; }
    if (now - lastTick_ >= 250) { lastTick_ = now; d.add(R_SEGMENTS); }
    if (marquee_ && now - lastScroll_ >= 50) { lastScroll_ = now; d.add(Rect(M + 60, 202, 240 - M - 60, 20)); }   // ticker ~20 fps
}

static int wrapLines(const char* s, const Font& f, int16_t maxW, char out[][64], int maxLines) {
    int n = 0;
    char line[128] = "";
    char word[64];
    const char* p = s;
    while (*p && n < maxLines) {
        int k = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && k < 63) word[k++] = *p++;
        word[k] = 0;
        if (!k) break;
        char trial[128];
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        if (Canvas::textWidth(trial, f) <= maxW) { snprintf(line, sizeof line, "%s", trial); continue; }
        if (line[0]) { snprintf(out[n++], 64, "%.63s", line); }
        snprintf(line, sizeof line, "%s", word);
    }
    if (line[0] && n < maxLines) snprintf(out[n++], 64, "%.63s", line);
    else if (n == maxLines && (line[0] || *p)) {   // truncated: ellipsize the last line
        char tmp[72];
        snprintf(tmp, sizeof tmp, "%s\xE2\x80\xA6", out[n - 1]);
        Canvas::ellipsize(tmp, f, maxW, out[n - 1], 64);
    }
    return n;
}

void NewsScreen::render(Canvas& c, const Theme& t) {
    char _t[40];
    const BridgeData& b = g_state.bridge;
    if (!b.news) {
        header(c, t, "News", nullptr, t.accent2, wifiBars());
        emptyState(c, t, iconNews, PSTR("Waiting for headlines"),
                   b.online ? PSTR("Headlines appear after the next update") : PSTR("Cloud relay offline"));
        return;
    }
    const uint8_t cur = idx_ % b.news;
    const NewsItem& it = b.nw[cur];
    char buf[48];
    snprintf(buf, sizeof buf, "%s  \xC2\xB7  %u OF %u", it.cat[0] ? it.cat : "NEWS", unsigned(cur + 1), unsigned(b.news));
    header(c, t, buf, nullptr, t.accent2, wifiBars(), nullptr, g_state.bridge.nwAt);
    segments(c, t, b.news, cur, float(millis() - storyAt_) / storyMs());
    const Rect win = c.window();
    if (win.b() <= 34) return;                                   // segment-only redraws stop here

    // source line first (editorial), then the headline
    c.fillCircle(M + 3, 54, 3, t.accent2);
    float xx = M + 12 + c.text(M + 12, 58, it.src, FONT_L_BODY, t.text2) + 6;
    if (it.ageMin >= 0) {
        xx += c.text(int16_t(xx), 58, "\xC2\xB7", FONT_L_BODY, t.text3) + 6;
        if (it.ageMin < 60) snprintf(buf, sizeof buf, "%ld min ago", long(it.ageMin));
        else if (it.ageMin < 48 * 60) snprintf(buf, sizeof buf, "%ld h ago", long(it.ageMin / 60));
        else snprintf(buf, sizeof buf, "%ld d ago", long(it.ageMin / 1440));
        c.text(int16_t(xx), 58, buf, FONT_L_BODY, t.text3);
    }
    // largest size that shows the whole headline: Outfit 23 x 4, display 18 x 5, title 15 x 6
    struct HeadFit { const Font* f; int maxLines; float by, step; };
    static const HeadFit FITS[] = {{&FONT_L_HEAD, 4, 94, 29}, {&FONT_L_DISPLAY, 5, 90, 23}, {&FONT_L_TITLE, 6, 88, 19}};
    char lines[6][64];
    const HeadFit* hf = &FITS[0];
    int n = 0;
    for (const HeadFit& f : FITS) {
        hf = &f;
        n = wrapLines(it.head, *f.f, 212, lines, f.maxLines);
        size_t chars = 0;
        for (int i = 0; i < n; i++) chars += strlen(lines[i]) + 1;
        if (chars >= strlen(it.head) + 1 && !strstr(lines[n - 1], "\xE2\x80\xA6")) break;
    }
    for (int i = 0; i < n; i++) c.text(M, int16_t(hf->by + i * hf->step), lines[i], *hf->f, t.text);

    // up next: hairline + label + marquee, no card
    hairline(c, t, 196);
    label(c, M, 216, FS("UP NEXT"), t.highlight);
    const NewsItem& nx = b.nw[(idx_ + 1) % b.news];
    char nxt[140];
    snprintf(nxt, sizeof nxt, "%s%s%s", nx.cat[0] && strcmp(nx.cat, it.cat) ? nx.cat : "",
             nx.cat[0] && strcmp(nx.cat, it.cat) ? " \xC2\xB7 " : "", nx.head);
    const int16_t tx0 = M + 62, tx1 = 226, tyN = 216;
    const int16_t tw = Canvas::textWidth(nxt, FONT_L_BODY);
    if (tw <= tx1 - tx0) {
        c.text(tx0, tyN, nxt, FONT_L_BODY, t.text2);
        marquee_ = false;
    } else {
        const int16_t cycle = tw + 24;
        uint32_t el = millis() - storyAt_;
        int16_t off = el < 2000 ? 0 : int16_t(((el - 2000) * 30 / 1000) % uint32_t(cycle));
        const int16_t fade = 22;
        // clip to the strip; the last `fade` px ramp the opacity down in 4 px steps
        for (int16_t sx = tx0; sx < tx1; sx += (sx < tx1 - fade ? int16_t(tx1 - fade - tx0) : 4)) {
            int16_t sw = sx < tx1 - fade ? int16_t(tx1 - fade - sx) : 4;
            c.setClip(Rect(sx, int16_t(tyN - 14), sw, 20));
            uint8_t a = sx < tx1 - fade ? 255 : uint8_t(255 * (tx1 - sx) / fade);
            c.setOpacity(a);
            c.text(int16_t(tx0 - off), tyN, nxt, FONT_L_BODY, t.text2);
            c.text(int16_t(tx0 - off + cycle), tyN, nxt, FONT_L_BODY, t.text2);
        }
        c.clearClip();
        c.setOpacity(255);
        marquee_ = true;
    }
}

// ================================================================ CALENDAR
void CalendarScreen::background(Canvas& c, const Theme& t) { static Light L[2]; dashLights(t, L); paintBackground(c, t, L, 2); }
void CalendarScreen::enter(uint32_t now) { (void)now; ver_ = g_state.bridge.version; minuteTick(lastMin_); }
void CalendarScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    if (minuteTick(lastMin_)) d.addAll();          // countdown + now marker move every minute
    if (g_state.bridge.version != ver_) { ver_ = g_state.bridge.version; d.addAll(); }
}

void CalendarScreen::render(Canvas& c, const Theme& t) {
    const BridgeData& b = g_state.bridge;
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char title[24], mon[6], wd[6];
    strftime(wd, sizeof wd, "%a", &tm);
    strftime(mon, sizeof mon, "%b", &tm);
    snprintf(title, sizeof title, "%s \xC2\xB7 %d %s", wd, tm.tm_mday, mon);
    header(c, t, title, iconCalendar, t.highlight, wifiBars());
    if (!b.events) {
        emptyState(c, t, iconCalendar, PSTR("No upcoming events"),
                   b.online ? PSTR("Your next events appear here") : PSTR("Companion bridge offline"));
        return;
    }

    // next event: first one not yet ended
    int ni = -1;
    for (int i = 0; i < b.events; i++) if (!b.ca[i].allDay && b.ca[i].end > uint32_t(now)) { ni = i; break; }
    if (ni < 0) { c.text(120, 120, "No more meetings today", FONT_L_BODY, t.text3, Align::Center); return; }
    const CalEvent& e = b.ca[ni];
    const float cy0 = 34, ch = 66;
    glassCard(c, t, M, cy0, 212, ch, 16);
    c.setOpacity(t.dark ? 18 : 15);
    c.fillRoundRect(M, cy0, 212, ch, 16, t.accent2);
    c.setOpacity(255);
    c.glow(M + 33, cy0 + 33, 31, t.accent2, t.dark ? 45 : 55);
    long mins = (long(e.start) - long(now)) / 60;
    label(c, M + 14, int16_t(cy0 + 19), mins > 0 ? "NEXT" : "NOW", t.highlight);
    char fit[48];
    Canvas::ellipsize(e.title, FONT_L_TITLE, 150, fit, sizeof fit);
    c.text(M + 14, int16_t(cy0 + 40), fit, FONT_L_TITLE, t.text);
    struct tm st;
    time_t ts = time_t(e.start);
    localtime_r(&ts, &st);
    char meta[48];
    if (e.where[0]) snprintf(meta, sizeof meta, "%02d:%02d  \xC2\xB7  %s", st.tm_hour, st.tm_min, e.where);
    else snprintf(meta, sizeof meta, "%02d:%02d", st.tm_hour, st.tm_min);
    Canvas::ellipsize(meta, FONT_L_BODY, 150, fit, sizeof fit);
    c.text(M + 14, int16_t(cy0 + 57), fit, FONT_L_BODY, t.text2);
    // countdown ring (within the hour)
    const float rx = 196, ry = cy0 + 33, rr = 22;
    c.setOpacity(230);
    c.ring(rx, ry, rr, 3.5f, t.surfaceHi);
    c.setOpacity(255);
    if (mins > 0 && mins <= 60) c.arcGradient(rx, ry, rr, 3.5f, -90, -90 + 360.f * (1 - mins / 60.f), t.accent2, t.highlight);
    char nb[16];
    if (mins > 0 && mins < 100) snprintf(nb, sizeof nb, "%ld", mins); else if (mins > 0) snprintf(nb, sizeof nb, "%ldh", mins / 60);
    else strcpy(nb, "0");
    c.text(int16_t(rx), int16_t(ry + 3), nb, FONT_L_NUM_S, t.text, Align::Center);
    c.text(int16_t(rx), int16_t(ry + 14), mins >= 100 ? "" : "MIN", FONT_L_MICRO, t.text3, Align::Center);

    // day strip 08 -> 20 with event blocks and a now marker
    const float x0 = M, x1 = 226, sy = 112;
    auto X = [&](float h) { return x0 + (h - 8) / 12 * (x1 - x0); };
    c.setOpacity(217);
    c.fillRoundRect(x0, sy, x1 - x0, 6, 3, t.surfaceHi);
    c.setOpacity(255);
    for (int i = 0; i < b.events; i++) {
        time_t a = time_t(b.ca[i].start), z = time_t(b.ca[i].end);
        struct tm ta, tz;
        localtime_r(&a, &ta); localtime_r(&z, &tz);
        if (ta.tm_yday != tm.tm_yday) continue;
        float ha = ta.tm_hour + ta.tm_min / 60.f, hz = tz.tm_hour + tz.tm_min / 60.f;
        if (hz < 8 || ha > 20) continue;
        if (ha < 8) ha = 8;
        if (hz > 20) hz = 20;
        bool past = b.ca[i].end < uint32_t(now);
        c.setOpacity(past ? 153 : 255);
        float w = X(hz) - X(ha);
        c.fillRoundRect(X(ha), sy, w < 6 ? 6 : w, 6, 3, past ? t.text3 : (i == ni ? t.accent2 : t.accent));
        c.setOpacity(255);
    }
    float nh = tm.tm_hour + tm.tm_min / 60.f;
    if (nh >= 8 && nh <= 20) {
        c.glow(X(nh), sy + 3, 10, t.highlight, 140);
        c.fillRoundRect(X(nh) - 1, sy - 4, 2.4f, 14, 1.2f, t.highlight);
    }
    for (int h = 8; h <= 20; h += 4) {
        char hb[4]; snprintf(hb, sizeof hb, "%02d", h);
        c.text(int16_t(X(h)), int16_t(sy + 20), hb, FONT_L_LABEL, t.text3, h == 8 ? Align::Left : (h == 20 ? Align::Right : Align::Center), 1);
    }
    // reminder pill: the first all-day event today (birthdays, bills, holidays)
    for (int i = 0; i < b.events; i++) {
        if (!b.ca[i].allDay) continue;
        const float ry = 204;
        c.setOpacity(t.glassA);
        c.fillRoundRect(M, ry, 212, 24, 12, t.surface);
        c.setOpacity(204);
        c.strokeRoundRect(M, ry, 212, 24, 12, 1, t.stroke);
        c.setOpacity(255);
        iconBell(c, M + 15, ry + 12, 13, t.warn);
        char rf[48];
        Canvas::ellipsize(b.ca[i].title, FONT_L_BODY, 140, rf, sizeof rf);
        c.text(M + 28, int16_t(ry + 17), rf, FONT_L_BODY, t.text);
        label(c, 226 - 12, int16_t(ry + 16), "TODAY", t.warn, Align::Right);
        break;
    }

    // agenda with timeline rail
    const float ly = 152, railX = M + 4;
    c.setOpacity(179);
    c.fillRect(int16_t(railX), int16_t(ly - 6), 1, int16_t(2 * 22 + 8), t.stroke);
    c.setOpacity(255);
    int row = 0;
    for (int i = 0; i < b.events && row < 2; i++) {
        const CalEvent& ev = b.ca[i];
        if (ev.allDay) continue;
        const float y = ly + row++ * 21;
        bool done = ev.end < uint32_t(now), next = i == ni;
        if (next) { c.glow(railX, y - 4, 9, t.accent2, 140); c.fillCircle(railX, y - 4, 3.6f, t.accent2); }
        else if (done) { c.fillCircle(railX, y - 4, 3.0f, t.surfaceHi); c.setOpacity(204); c.ring(railX, y - 4, 3.2f, 1.2f, t.text3); c.setOpacity(255); }
        else { c.fillCircle(railX, y - 4, 3.0f, t.dark ? t.bg : t.surface); c.ring(railX, y - 4, 3.4f, 1.4f, t.accent); }
        time_t es = time_t(ev.start);
        struct tm et;
        localtime_r(&es, &et);
        char tb[8]; snprintf(tb, sizeof tb, "%02d:%02d", et.tm_hour, et.tm_min);
        c.text(M + 16, int16_t(y), tb, FONT_L_BODY, done ? t.text3 : t.text2);
        Canvas::ellipsize(ev.title, FONT_L_BODY, next && mins > 0 ? 82 : 150, fit, sizeof fit);
        c.text(M + 58, int16_t(y), fit, FONT_L_BODY, done ? t.text3 : t.text);
        if (next && mins > 0 && mins < 600) {
            char in[24];
            if (mins < 60) snprintf(in, sizeof in, "IN %ld MIN", mins); else snprintf(in, sizeof in, "IN %ldH", mins / 60);
            label(c, 226, int16_t(y), in, t.highlight, Align::Right);
        }
    }
}

// ================================================================ QUOTE / INTERNET / NETWORK (bridge)

static void iconQuote(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    for (float dx : {-0.22f, 0.22f}) {
        c.fillCircle(cx + dx * s, cy + 0.08f * s, 0.15f * s, col);
        c.line(cx + dx * s + 0.12f * s, cy + 0.02f * s, cx + dx * s + 0.02f * s, cy - 0.32f * s, 0.12f * s, col);
    }
}
static void iconLan(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    c.line(cx, cy - 0.30f * s, cx - 0.32f * s, cy + 0.28f * s, 0.08f * s, col);
    c.line(cx, cy - 0.30f * s, cx + 0.32f * s, cy + 0.28f * s, 0.08f * s, col);
    c.fillCircle(cx, cy - 0.30f * s, 0.14f * s, col);
    c.fillCircle(cx - 0.32f * s, cy + 0.28f * s, 0.14f * s, col);
    c.fillCircle(cx + 0.32f * s, cy + 0.28f * s, 0.14f * s, col);
}

// --- Quote of the day
void QuoteScreen::background(Canvas& c, const Theme& t) {
    static Light L[2];
    L[0] = {20, 40, 140, t.accent, uint8_t(t.dark ? 50 : 40)};
    L[1] = {230, 230, 120, t.lights[1].color, t.lights[1].strength};
    paintBackground(c, t, L, 2);
}
void QuoteScreen::enter(uint32_t now) { (void)now; minuteTick(lastMin_); ver_ = g_state.bridge.version; }
void QuoteScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    if (g_state.bridge.version != ver_) { ver_ = g_state.bridge.version; d.addAll(); }
}
void QuoteScreen::render(Canvas& c, const Theme& t) {
    const QuoteData& q = g_state.bridge.qt;
    header(c, t, "Quote", nullptr, t.accent, wifiBars());
    if (!q.text[0]) {
        emptyState(c, t, iconQuote, PSTR("No quote yet"), PSTR("Today's quote arrives with the bridge"));
        return;
    }
    // large soft quote mark, top left
    c.setOpacity(t.dark ? 70 : 90);
    iconQuote(c, t, M + 14, 50, 30, t.accent);
    c.setOpacity(255);
    // largest size that shows the whole quote
    struct Fit { const Font* f; int maxLines; float step; };
    static const Fit FITS[] = {{&FONT_L_HEAD, 5, 28}, {&FONT_L_DISPLAY, 6, 23}, {&FONT_L_TITLE, 7, 19}, {&FONT_L_BODY, 8, 17}};
    char lines[8][64];
    const Fit* fit = &FITS[0];
    int n = 0;
    for (const Fit& f : FITS) {
        fit = &f;
        n = wrapLines(q.text, *f.f, 208, lines, f.maxLines);
        size_t chars = 0;
        for (int i = 0; i < n; i++) chars += strlen(lines[i]) + 1;
        if (n && chars >= strlen(q.text) + 1 && !strstr(lines[n - 1], "\xE2\x80\xA6")) break;
    }
    const float block = fit->step * (n - 1);
    float y0 = 132 - block / 2 + fit->f->asc / 2.f;
    if (y0 < 74 + fit->f->asc / 2.f) y0 = 74 + fit->f->asc / 2.f;
    for (int i = 0; i < n; i++) c.text(M, int16_t(y0 + i * fit->step), lines[i], *fit->f, t.text);
    if (q.author[0]) {
        hairline(c, t, 200);
        char a[40];
        Canvas::ellipsize(q.author, FONT_L_TITLE, 200, a, sizeof a);
        c.fillRoundRect(M, 213, 14, 2, 1, t.highlight);
        c.text(M + 22, 219, a, FONT_L_TITLE, t.text2);
    }
}

// --- Home network
void LanScreen::background(Canvas& c, const Theme& t) {
    static Light L[3];
    L[0] = {40, 100, 140, t.accent, uint8_t(t.dark ? 40 : 36)};
    L[1] = {220, 0, 110, t.lights[0].color, t.lights[0].strength};
    L[2] = {8, 240, 90, t.lights[1].color, uint8_t(t.lights[1].strength / 2)};
    paintBackground(c, t, L, 3);
}
void LanScreen::enter(uint32_t now) { (void)now; minuteTick(lastMin_); ver_ = g_state.bridge.version; }
void LanScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    if (g_state.bridge.version != ver_) { ver_ = g_state.bridge.version; d.addAll(); }
}
void LanScreen::render(Canvas& c, const Theme& t) {
    char _t[48];
    const LanData& l = g_state.bridge.ln;
    header(c, t, "Network", nullptr, t.highlight, wifiBars(), nullptr, g_state.bridge.lnAt);
    if (!l.count) {
        emptyState(c, t, iconLan, PSTR("Scanning the network"), PSTR("Devices appear after the first scan"));
        return;
    }
    char buf[40];
    label(c, M, 50, FS("DEVICES ONLINE"), t.text3);
    snprintf_P(buf, sizeof buf, PSTR("%u"), unsigned(l.count));
    hero(c, t, M - 3, 124, buf, FONT_L_CLOCK_XL);
    // router + new devices, right column
    label(c, 226, 76, FS("ROUTER"), t.text3, Align::Right);
    c.text(226, 96, l.gwMs >= 0 ? "Online" : "Not seen", FONT_L_TITLE, l.gwMs >= 0 ? t.good : t.warn, Align::Right);
    if (l.newCount) {
        snprintf_P(buf, sizeof buf, PSTR("%u joined"), unsigned(l.newCount));   // since the clock started
        c.glow(226 - Canvas::textWidth(buf, FONT_L_BODY) - 8, 118, 7, t.highlight, 120);
        c.fillCircle(226 - Canvas::textWidth(buf, FONT_L_BODY) - 8, 118, 2.6f, t.highlight);
        c.text(226, 122, buf, FONT_L_BODY, t.highlight, Align::Right);
    }
    // devices: 2 columns x 4 rows on hairlines, new ones first, then the router (LanScanService order);
    // names are the last 3 bytes of each device's MAC address
    hairline(c, t, 140);
    for (uint8_t i = 0; i < l.devN; i++) {
        const LanDevice& d = l.dev[i];
        const float x = M + (i % 2) * 110, y = 159 + (i / 2) * 19;
        const uint16_t dc = d.isNew ? t.highlight : t.good;
        if (d.isNew) c.glow(x + 3, y - 4, 7, dc, 120);
        c.fillCircle(x + 3, y - 4, 2.4f, dc);
        char nm[24], ip[6];
        snprintf_P(ip, sizeof ip, PSTR(".%u"), unsigned(d.ip));
        const int16_t iw = Canvas::textWidth(ip, FONT_L_MICRO);
        Canvas::ellipsize(d.name, FONT_L_BODY, int16_t(102 - 12 - iw - 6), nm, sizeof nm);
        c.text(int16_t(x + 11), int16_t(y), nm, FONT_L_BODY, t.text);
        c.text(int16_t(x + 102), int16_t(y), ip, FONT_L_MICRO, t.text3, Align::Right);
    }
    if (l.count > l.devN) {
        snprintf_P(buf, sizeof buf, PSTR("+%u more"), unsigned(l.count - l.devN));
        label(c, 226, 50, buf, t.text3, Align::Right);
    }
}
