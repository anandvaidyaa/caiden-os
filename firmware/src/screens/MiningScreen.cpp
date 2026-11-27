#include "MiningScreen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../ui/Lumen.h"
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

using namespace lumen;

#define FS(s) flashStr(_t, sizeof _t, PSTR(s))

static const float DEG = 0.01745329f;
static const float RCX = 64, RCY = 104, RR = 44;           // the orbit ring
static const Rect R_ORBIT(10, 52, 110, 106);                // ring + hash rate (redrawn while spinning)
static const Rect R_STATS(124, 44, 116, 118);               // share counts + chain
static const Rect R_FOOT(0, 160, 240, 80);                  // sparkline + earnings + lottery

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
    c.line(cx - s * 0.35f, cy + s * 0.35f, cx + s * 0.2f, cy - s * 0.2f, 1.6f, col);          // handle
    c.arc(cx + s * 0.05f, cy - s * 0.05f, s * 0.45f, 1.8f, 200, 340, col);                    // head
}

// 1234 -> "1.23" + "KH/S", 950 -> "950" + "H/S" (the number font has digits only)
static const char* rateText(char* b, size_t n, float hr) {
    if (hr >= 1000) { snprintf(b, n, "%.2f", hr / 1000.f); return "KH/S"; }
    snprintf(b, n, "%.0f", hr);
    return "H/S";
}

// "1 in 4.8 quadrillion": the odds that this clock finds a Bitcoin block today
static void lotteryText(char* b, size_t n, float ducoHr, float btcHr) {
    // a Bitcoin hash is two SHA-256 runs over 80 bytes: about 3x the work of one DUCO-S1 SHA-1 hash
    const double ours = ducoHr / 3.0;
    if (ours <= 0 || btcHr <= 0) { snprintf_P(b, n, PSTR("Bitcoin odds: waiting for data")); return; }
    const double oneIn = btcHr / (ours * 144.0);             // 144 blocks a day
    // number words in flash, 12 characters apart
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

void MiningScreen::background(Canvas& c, const Theme& t) {
    static Light L[3];
    L[0] = {64, 104, 130, t.highlight, uint8_t(t.dark ? 30 : 34)};
    L[1] = {220, 0, 110, t.lights[0].color, t.lights[0].strength};
    L[2] = {8, 240, 90, t.lights[1].color, uint8_t(t.lights[1].strength / 2)};
    paintBackground(c, t, L, 3);
}

void MiningScreen::enter(uint32_t now) {
    minuteTick(lastMin_);
    ver_ = g_state.mine.version;
    lastSpin_ = now;
    seenAcc_ = g_state.mine.acc;
    seenRej_ = g_state.mine.rej;
}

void MiningScreen::update(uint32_t now, DirtySet& d) {
    const MiningData& m = g_state.mine;
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    // share results -> the chain of blocks (newest first)
    while (seenAcc_ < m.acc || seenRej_ < m.rej) {
        const bool good = seenAcc_ < m.acc;   // order within one pass doesn't matter for 8 blocks
        if (good) seenAcc_++; else seenRej_++;
        chain_ = uint8_t(chain_ << 1 | (good ? 1 : 0));
        if (chainN_ < 8) chainN_++;
    }
    if (m.version != ver_) { ver_ = m.version; d.add(R_STATS); d.add(R_FOOT); d.add(R_ORBIT); }
    // spin: speed follows the hash rate (about one turn per 4 s at 2 kH/s), ~12 fps, ring area only
    if (m.status == MineStatus::Mining && now - lastSpin_ >= 80) {
        const float dt = (now - lastSpin_) / 1000.f;
        lastSpin_ = now;
        float speed = 30 + m.hr * 0.045f;                    // degrees per second
        if (speed > 360) speed = 360;
        phase_ = fmodf(phase_ + speed * dt, 360.f);
        d.add(R_ORBIT);
    } else if (m.status != MineStatus::Mining) {
        lastSpin_ = now;
    }
    if (m.lastShareAt && now - m.lastShareAt < 700) d.add(R_ORBIT);   // share flash fades out
}

void MiningScreen::render(Canvas& c, const Theme& t) {
    char _t[48];
    char buf[48];
    const MiningData& m = g_state.mine;
    const Config& cfg = g_config.get();
    header(c, t, "Mining", iconPick, t.highlight, wifiBars());
    if (m.status == MineStatus::Off) {
        emptyState(c, t, iconPick, PSTR("Mining is off"), PSTR("Turn it on under Feeds, Mining"));
        return;
    }
    if (m.status == MineStatus::NoAccount) {
        emptyState(c, t, iconPick, PSTR("No Duino-Coin account"), PSTR("Add your username under Feeds"));
        return;
    }
    const bool live = m.status == MineStatus::Mining;
    const uint32_t now = millis();

    // ---- orbit: faint track, spinning comet arc, share flash, hash rate inside
    c.setOpacity(70);
    c.ring(RCX, RCY, RR, 3, t.surfaceHi);
    c.setOpacity(255);
    dashedV(c, RCX, RCY - RR - 6, RCY - RR + 2, t.text3, 120, 2, 2);
    if (live) {
        c.arcGradient(RCX, RCY, RR, 3, phase_ - 110, phase_, t.accent, t.highlight);
        const float hx = RCX + (RR - 1.5f) * cosf(phase_ * DEG), hy = RCY + (RR - 1.5f) * sinf(phase_ * DEG);
        c.glow(hx, hy, 12, t.highlight, 150);
        c.fillCircle(hx, hy, 3.4f, t.text);
    }
    if (m.lastShareAt && now - m.lastShareAt < 700) {        // a block "lands": glow at the top of the ring
        const uint8_t a = uint8_t(255 - 255 * (now - m.lastShareAt) / 700);
        const uint16_t col = m.lastGood ? t.good : t.bad;
        c.glow(RCX, RCY - RR, 16, col, uint8_t(a * 0.7f));
        c.setOpacity(a);
        c.fillRoundRect(RCX - 5, RCY - RR - 5, 10, 10, 2, col);
        c.setOpacity(255);
    }
    const char* unit = rateText(buf, sizeof buf, m.hr);
    c.text(int16_t(RCX), int16_t(RCY + 8), buf, FONT_L_NUM_L, live ? t.text : t.text3, Align::Center);
    label(c, int16_t(RCX), int16_t(RCY + 26), unit, t.text3, Align::Center);
    if (!live) {
        const char* s = m.status == MineStatus::Paused ? m.why :
                        m.status == MineStatus::WaitingPool ? "Finding a pool" :
                        m.status == MineStatus::Connecting ? "Connecting" : m.why;
        c.text(int16_t(RCX), int16_t(RCY - 14), s, FONT_L_MICRO, m.status == MineStatus::Error ? t.bad : t.warn,
               Align::Center);
    }

    // ---- shares
    const uint32_t total = m.acc + m.rej;
    label(c, 130, 60, FS("ACCEPTED"), t.text3);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)m.acc);
    c.text(130, 82, buf, FONT_L_NUM_M, t.good);
    label(c, 130, 102, FS("REJECTED"), t.text3);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)m.rej);
    int16_t w = c.text(130, 124, buf, FONT_L_NUM_M, m.rej ? t.bad : t.text2);
    if (total) {
        snprintf_P(buf, sizeof buf, PSTR("%lu%% good"), (unsigned long)(m.acc * 100 / total));
        c.text(int16_t(130 + w + 8), 124, buf, FONT_L_MICRO, t.text3);
    }
    // the chain: last 8 shares as blocks, newest on the left
    for (uint8_t i = 0; i < 8; i++) {
        const float x = 130 + i * 12, y = 136;
        if (i < chainN_) {
            const bool good = (chain_ >> i) & 1;
            c.fillRoundRect(x, y, 9, 9, 2, good ? t.good : t.bad);
        } else {
            c.setOpacity(70);
            c.fillRoundRect(x, y, 9, 9, 2, t.surfaceHi);
            c.setOpacity(255);
        }
    }
    snprintf_P(buf, sizeof buf, PSTR("DIFF %u"), unsigned(m.diff));
    label(c, 226, 60, buf, t.text3, Align::Right);

    // ---- 24 h hash rate (hourly averages) + the current rate as the last point
    hairline(c, t, 162);
    const uint8_t n = m.histN;
    float mx = m.hr > 1 ? m.hr : 1;
    for (uint8_t i = 0; i < n; i++) if (m.hist[i] > mx) mx = m.hist[i];
    const float x0 = M, x1 = 226, yb = 192, h = 20;
    const uint8_t pts = n + 1;
    float px = x0, py = yb;
    for (uint8_t i = 0; i < pts; i++) {
        const float v = i < n ? m.hist[i] : m.hr;
        const float x = pts > 1 ? x0 + (x1 - x0) * i / float(pts - 1) : x1;
        const float y = yb - h * v / mx;
        if (i) c.line(px, py, x, y, 1.6f, t.highlight);
        px = x; py = y;
    }
    c.fillCircle(px, py, 2.6f, t.text);
    label(c, M, 176, n ? "24 H" : "STARTED", t.text3);

    // ---- earnings (real, even when 0) and the Bitcoin lottery
    if (m.relayOk) {
        const float inr = m.today * m.inr;
        if (m.inr > 0) snprintf_P(buf, sizeof buf, PSTR("%.3f DUCO today  \xC2\xB7  INR %.6f"), m.today, inr);   // tiny on purpose: honest
        else snprintf_P(buf, sizeof buf, PSTR("%.4f DUCO today"), m.today);
    } else {
        snprintf_P(buf, sizeof buf, PSTR("Earnings: waiting for the relay"));
    }
    c.text(M, 212, buf, FONT_L_BODY, t.text2);
    lotteryText(buf, sizeof buf, m.hr, m.btcHr);
    c.text(M, 229, buf, FONT_L_MICRO, t.text3);
    (void)cfg;
}
