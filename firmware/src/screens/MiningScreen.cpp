#include "MiningScreen.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../ui/Lumen.h"
#include "../state/AppState.h"

using namespace lumen;

#define FS(s) flashStr(_t, sizeof _t, PSTR(s))

static const float DEG = 0.01745329f, PI_F = 3.14159265f;

// ---- scene geometry (the miner faces right, the rock is in front of him)
static const float GROUND = 132;
static const float SHX = 84, SHY = 97;          // shoulder: the swing pivot
static const float HANDLE = 32;                 // pickaxe handle length
static const float HITX = 115, HITY = 111;      // where the pick meets the rock
static const float ROCKX = 134, ROCKY = 116;    // rock centre
static const float COINX = 214, COINY = 46;     // the coin counter, top right of the scene
// redraw areas while animating
static const Rect R_SWING(46, 56, 76, 78);      // arm + pickaxe sweep
static const Rect R_SPARK(104, 94, 30, 30);     // sparks at the hit point
static const Rect R_COIN(126, 34, 104, 86);     // the flying coin's path
static const Rect R_BUBBLE(14, 34, 116, 24);    // speech bubble above the miner
static const Rect R_LOWER(0, 138, 240, 102);    // numbers, chart, earnings

static const uint16_t SKIN = rgb(0xF1C27D), WOOD = rgb(0x9A6A3A), METAL = rgb(0xC9D3E3), STONE = rgb(0x5B6478),
                      STONE_HI = rgb(0x7D879C), BOOT = rgb(0x2A2F3A);

static bool minuteTick(int& lastMin) {
    time_t tt = time(nullptr);
    struct tm tm;
    localtime_r(&tt, &tm);
    if (tm.tm_min == lastMin) return false;
    lastMin = tm.tm_min;
    return true;
}

static void iconPick(Canvas& c, const Theme& t, float cx, float cy, float s, uint16_t col) {
    (void)t;
    c.line(cx - s * 0.35f, cy + s * 0.35f, cx + s * 0.2f, cy - s * 0.2f, 1.6f, col);
    c.arc(cx + s * 0.05f, cy - s * 0.05f, s * 0.45f, 1.8f, 200, 340, col);
}

static float ease(float x) { return x < 0 ? 0 : x > 1 ? 1 : x * x * (3 - 2 * x); }

// Pickaxe angle (degrees, 0 = right, + = down) through one swing: slow wind-up, fast strike, short rest.
static float swingAngle(float p, bool working) {
    if (!working) return 62;                                     // resting, pick on the ground
    if (p < 0.62f) return 22 - 142 * ease(p / 0.62f);           // wind up over the shoulder
    if (p < 0.74f) return -120 + 142 * ((p - 0.62f) / 0.12f);  // strike
    return 22;                                                   // on the rock
}

// 1234 -> "1.23" + "KH/S", 950 -> "950" + "H/S" (the number font has digits only)
static const char* rateText(char* b, size_t n, float hr) {
    if (hr >= 1000) { snprintf_P(b, n, PSTR("%.2f"), hr / 1000.f); return "KH/S"; }
    snprintf_P(b, n, PSTR("%.0f"), hr);
    return "H/S";
}

// "BTC block today: 1 in 4.8 quadrillion"
static void lotteryText(char* b, size_t n, float ducoHr, float btcHr) {
    // a Bitcoin hash is two SHA-256 runs over 80 bytes: about 3x the work of one DUCO-S1 SHA-1 hash
    const double ours = ducoHr / 3.0;
    if (ours <= 0 || btcHr <= 0) { snprintf_P(b, n, PSTR("Bitcoin odds: waiting for data")); return; }
    const double oneIn = btcHr / (ours * 144.0);                 // 144 blocks a day
    static const char W[] PROGMEM = "\0           thousand\0   million\0    billion\0    trillion\0   "
                                    "quadrillion\0quintillion\0sextillion\0 septillion";
    double v = oneIn;
    int k = 0;
    while (v >= 1000 && k < 8) { v /= 1000; k++; }
    char word[13];
    strncpy_P(word, W + 12 * k, sizeof word - 1);
    word[sizeof word - 1] = 0;
    if (k == 0) snprintf_P(b, n, PSTR("BTC block today: 1 in %.0f"), v);
    else snprintf_P(b, n, PSTR("BTC block today: 1 in %.1f %s"), v, word);
}

static void coin(Canvas& c, const Theme& t, float x, float y, float r) {
    c.glow(x, y, r * 2.2f, t.sun, 120);
    c.fillCircle(x, y, r, t.sun);
    c.ring(x, y, r - 1.2f, 1.2f, t.sun2);
    c.text(int16_t(x), int16_t(y + r * 0.45f), "D", FONT_L_MICRO, t.bg, Align::Center);
}

void MiningScreen::background(Canvas& c, const Theme& t) {
    static Light L[3];
    L[0] = {100, 96, 120, t.sun, uint8_t(t.dark ? 22 : 26)};     // warm lamp light in the mine
    L[1] = {220, 0, 110, t.lights[0].color, t.lights[0].strength};
    L[2] = {8, 240, 90, t.lights[1].color, uint8_t(t.lights[1].strength / 2)};
    paintBackground(c, t, L, 3);
}

void MiningScreen::enter(uint32_t now) {
    minuteTick(lastMin_);
    ver_ = g_state.mine.version;
    lastFrame_ = cycleFrom_ = now;
    seenAcc_ = g_state.mine.acc;
    seenRej_ = g_state.mine.rej;
}

void MiningScreen::update(uint32_t now, DirtySet& d) {
    const MiningData& m = g_state.mine;
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    if (m.rej != seenRej_) { seenRej_ = m.rej; oopsUntil_ = now + 1500; d.add(R_BUBBLE); }
    if (m.acc != seenAcc_) seenAcc_ = m.acc;
    if (oopsUntil_ && int32_t(now - oopsUntil_) >= 0) { oopsUntil_ = 0; d.add(R_BUBBLE); }
    if (m.version != ver_) { ver_ = m.version; d.add(R_LOWER); d.add(R_BUBBLE); d.add(R_COIN); }

    const bool working = m.status == MineStatus::Mining;
    if (now - lastFrame_ < 110) return;                           // ~9 fps: the drawing must leave time to mine
    lastFrame_ = now;
    // one swing every 1.4 s when slow, down to 0.65 s at 3 kH/s and above
    float hr = m.hr > 3000 ? 3000 : m.hr;
    const float period = 1400 - hr * 0.25f;
    const float before = swing_;
    swing_ = fmodf((now - cycleFrom_) / period, 1.f);
    if (working || before != 0) { d.add(R_SWING); d.add(R_SPARK); }
    if (!working) { swing_ = 0; cycleFrom_ = now; }
    const bool coinNow = m.lastShareAt && m.lastGood && now - m.lastShareAt < 900;
    if (coinNow || coinWas_) d.add(R_COIN);
    coinWas_ = coinNow;
}

void MiningScreen::render(Canvas& c, const Theme& t) {
    char _t[48];
    char buf[48];
    const MiningData& m = g_state.mine;
    header(c, t, "Mining", iconPick, t.highlight, wifiBars());
    if (m.status == MineStatus::Off) {
        emptyState(c, t, iconPick, PSTR("Mining is off"), PSTR("Turn it on under Feeds, Mining"));
        return;
    }
    if (m.status == MineStatus::NoAccount) {
        emptyState(c, t, iconPick, PSTR("No Duino-Coin account"), PSTR("Add your username under Feeds"));
        return;
    }
    const uint32_t now = millis();
    const bool working = m.status == MineStatus::Mining;

    // ---- ground and rock
    dashedH(c, M, 226, GROUND, t.text3, 110, 3, 3);
    c.fillCircle(ROCKX + 14, ROCKY + 6, 13, STONE);
    c.fillCircle(ROCKX, ROCKY, 17, STONE);
    c.fillCircle(ROCKX - 6, ROCKY - 6, 9, STONE_HI);
    c.fillRect(int16_t(ROCKX - 17), int16_t(ROCKY), 45, int16_t(GROUND - ROCKY), STONE);
    static const int8_t GEM[4][3] = {{-2, 6, 3}, {9, -3, 2}, {17, 9, 2}, {4, -10, 1}};   // x, y, size: uneven on purpose
    for (const auto& g : GEM) {                                    // crystals in the rock
        c.glow(ROCKX + g[0], ROCKY + g[1], 4 + g[2] * 1.5f, t.highlight, 100);
        c.line(ROCKX + g[0] - g[2], ROCKY + g[1] + g[2], ROCKX + g[0] + g[2], ROCKY + g[1] - g[2], 1.2f + g[2] * 0.6f, t.highlight);
    }

    // ---- the miner (static parts)
    c.line(76, 114, 72, GROUND - 1, 3.4f, BOOT);                  // legs
    c.line(82, 114, 88, GROUND - 1, 3.4f, BOOT);
    c.fillRoundRect(70, 92, 18, 24, 5, t.accent);                  // body
    c.fillRoundRect(70, 108, 18, 4, 1.5f, t.accent2);              // belt
    c.line(74, 98, 68, 112, 3.6f, t.accent);                       // back arm
    c.fillCircle(79, 84, 7.5f, SKIN);                              // head
    c.fillRoundRect(70, 74, 19, 9, 4.5f, t.sun);                   // helmet
    c.fillRect(68, 80, 23, 2, t.sun);                              // brim
    c.glow(90, 78, 10, t.sun, 150);                                // lamp
    c.fillCircle(89, 78, 2.2f, rgb(0xFFF6D8));
    c.fillCircle(82, 85, 1.2f, t.bg);                              // eye

    // ---- arm and pickaxe (animated)
    const float a = swingAngle(swing_, working) * DEG;
    const float dx = cosf(a), dy = sinf(a);
    const float tipX = SHX + dx * HANDLE, tipY = SHY + dy * HANDLE;
    const float hx = SHX + dx * 13, hy = SHY + dy * 13;
    c.line(SHX, SHY, tipX, tipY, 2.6f, WOOD);                      // handle
    const float px = -dy, py = dx;                                 // perpendicular: the pick's head
    c.line(tipX - px * 9 - dx * 2, tipY - py * 9 - dy * 2, tipX + px * 7, tipY + py * 7, 3.2f, METAL);
    c.line(SHX, SHY, hx, hy, 4.2f, t.accent);                      // arm
    c.fillCircle(hx, hy, 2.8f, SKIN);                              // hand

    // ---- sparks right after the strike
    if (working && swing_ >= 0.72f && swing_ < 0.95f) {
        const float k = (swing_ - 0.72f) / 0.23f;
        c.setOpacity(uint8_t(255 * (1 - k)));
        for (int i = 0; i < 6; i++) {
            const float sa = (150 + i * 24) * DEG, r = 3 + 13 * k;
            c.fillCircle(HITX + cosf(sa) * r, HITY + sinf(sa) * r - 6 * k, 1.5f, i % 2 ? t.sun : rgb(0xFFF6D8));
        }
        c.setOpacity(255);
    }

    // ---- accepted share: a coin pops out of the rock and flies to the counter
    coin(c, t, COINX, COINY, 7);                                   // the counter: DUCO earned today
    snprintf_P(buf, sizeof buf, PSTR("%.3f"), m.relayOk ? m.today : 0.f);
    c.text(int16_t(COINX - 12), int16_t(COINY + 6), buf, FONT_L_NUM_S, t.text, Align::Right);
    if (m.lastShareAt && m.lastGood && now - m.lastShareAt < 900) {
        const float k = (now - m.lastShareAt) / 900.f;
        const float x = ROCKX + (COINX - ROCKX) * k, y = (ROCKY - 18) + (COINY - ROCKY + 18) * k - 34 * sinf(PI_F * k);
        coin(c, t, x, y, 5 + 2 * k);
    }

    // ---- speech bubble: "Oops!" after a rejected share, or why mining is paused
    const char* say = nullptr;
    if (oopsUntil_) say = "Oops - bad share";
    else if (!working) say = m.status == MineStatus::WaitingPool ? "Finding a pool..." :
                             m.status == MineStatus::Connecting ? "Heading to the pool..." :
                             m.why[0] ? m.why : "Taking a break";
    if (say) {
        const int16_t w = Canvas::textWidth(say, FONT_L_MICRO) + 14;
        const float bx = 20, by = 38;
        c.fillRoundRect(bx, by, w, 16, 8, oopsUntil_ ? t.bad : t.surfaceHi);
        c.line(bx + 40, by + 15, bx + 46, by + 22, 2.4f, oopsUntil_ ? t.bad : t.surfaceHi);   // tail
        c.text(int16_t(bx + 7), int16_t(by + 11), say, FONT_L_MICRO, oopsUntil_ ? rgb(0xFFFFFF) : t.text);
    }

    // ---- numbers
    hairline(c, t, 140);
    label(c, M, 156, FS("HASH RATE"), t.text3);
    const char* unit = rateText(buf, sizeof buf, m.hr);
    const int16_t w = c.text(M - 1, 186, buf, FONT_L_NUM_L, working ? t.text : t.text3);
    label(c, int16_t(M + w + 5), 186, unit, t.text3);
    const uint32_t total = m.acc + m.rej;
    if (total) snprintf_P(_t, sizeof _t, PSTR("SHARES  %lu%% GOOD"), (unsigned long)(m.acc * 100 / total));
    else snprintf_P(_t, sizeof _t, PSTR("SHARES"));
    label(c, 226, 156, _t, t.text3, Align::Right);
    snprintf_P(buf, sizeof buf, PSTR("%lu"), (unsigned long)m.acc);
    int16_t gw = c.text(226, 172, buf, FONT_L_NUM_S, t.good, Align::Right);
    c.text(int16_t(226 - gw - 6), 172, "ok", FONT_L_MICRO, t.text3, Align::Right);
    snprintf_P(buf, sizeof buf, PSTR("%lu"), (unsigned long)m.rej);
    gw = c.text(226, 188, buf, FONT_L_NUM_S, m.rej ? t.bad : t.text3, Align::Right);
    c.text(int16_t(226 - gw - 6), 188, "bad", FONT_L_MICRO, t.text3, Align::Right);

    // 24 h hash rate (hourly averages + now), thin line under the numbers
    const uint8_t n = m.histN;
    float mx = m.hr > 1 ? m.hr : 1;
    for (uint8_t i = 0; i < n; i++) if (m.hist[i] > mx) mx = m.hist[i];
    const float x0 = M, x1 = 226, yb = 202, h = 8;
    const uint8_t pts = n + 1;
    float lx = x0, ly = yb;
    for (uint8_t i = 0; i < pts; i++) {
        const float v = i < n ? m.hist[i] : m.hr;
        const float x = pts > 1 ? x0 + (x1 - x0) * i / float(pts - 1) : x1;
        const float y = yb - h * v / mx;
        if (i) c.line(lx, ly, x, y, 1.4f, t.highlight);
        lx = x; ly = y;
    }
    c.fillCircle(lx, ly, 2.2f, t.text);

    // ---- earnings (real, even when tiny) and the Bitcoin lottery
    if (m.relayOk) {
        const float inr = m.today * m.inr;
        if (m.inr > 0) snprintf_P(buf, sizeof buf, PSTR("INR %.6f today  \xC2\xB7  %.2f total"), inr, m.bal);   // tiny on purpose: honest
        else snprintf_P(buf, sizeof buf, PSTR("%.3f DUCO today  \xC2\xB7  %.2f total"), m.today, m.bal);
    } else {
        snprintf_P(buf, sizeof buf, PSTR("Earnings: waiting for the relay"));
    }
    char fit[48];
    Canvas::ellipsize(buf, FONT_L_BODY, 212, fit, sizeof fit);
    c.text(M, 219, fit, FONT_L_BODY, t.text2);
    lotteryText(buf, sizeof buf, m.hr, m.btcHr);
    c.text(M, 234, buf, FONT_L_MICRO, t.text3);
}
