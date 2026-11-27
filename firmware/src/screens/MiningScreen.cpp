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

// ---- palette (fixed hues; the cave and floor follow the theme)
static const uint16_t SKIN = rgb(0xF4C99B), SKIN_SH = rgb(0xD9A273), BLUSH = rgb(0xFF8A8A), INK = rgb(0x2B2030),
                      WOOD = rgb(0xA8743F), WOOD_HI = rgb(0xC99358), WOOD_D = rgb(0x7A512A), METAL = rgb(0xD3DCEA),
                      METAL_D = rgb(0x8C97AB), STONE = rgb(0x56607A), STONE_HI = rgb(0x76819B), STONE_D = rgb(0x3A4258),
                      BOOT = rgb(0x262B36), CART = rgb(0x3B4560), CART_HI = rgb(0x56628A), WHITE = rgb(0xFFFFFF),
                      WARM = rgb(0xFFF6D8), BEAM = rgb(0xFFE9A8), LANTERN = rgb(0xFFD98A), SHINE = rgb(0xFFF7DA),
                      HELMET_HI = rgb(0xFFF3C4), BLACK = rgb(0x000000);

// ---- scene geometry
static const float GROUND = 134, SHX = 72, SHY = 99, HANDLE = 36, HITX = 107, HITY = 110;
static const float ROCKX = 128, ROCKY = 116, CARTX = 172, CARTY = 112;
static const float FROMX = 130, FROMY = 96, TOX = 196, TOY = 110;
static const float STRIKE0 = 0.62f, STRIKE1 = 0.74f;
// redraw areas while animating
// Measured on the clock (0.9.6): redrawing the whole miner and the ore 7x a second took 100-160 ms per frame
// and cut mining from ~40 to ~14 kH/s. Now only the swinging arm redraws, 4x a second, the sparks only
// during an impact, and the ore's shimmer once a second.
static const Rect R_SWING(24, 50, 96, 72);       // arm, pickaxe sweep incl. motion-blur ghosts, head, lamp beam
static const Rect R_SPARK(96, 90, 34, 44);       // sparks and chips at the hit point
static const Rect R_ORE(96, 76, 70, 60);         // ore shimmer, twinkles, crystal flash
static const Rect R_ZZZ(76, 50, 44, 44);         // dozing
static const Rect R_COIN(118, 50, 92, 70);       // the coin's flight path
static const Rect R_CART(164, 86, 70, 50);       // cart bounce, "+1"
static const Rect R_BUBBLE(12, 36, 156, 30);     // speech bubble
static const Rect R_HUD(136, 34, 96, 26);        // DUCO-today pill
static const Rect R_CARD(8, 148, 224, 90);       // glass stats card

static uint16_t mix(uint16_t a, uint16_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return pack565(uint8_t(r8(a) + (r8(b) - r8(a)) * t), uint8_t(g8(a) + (g8(b) - g8(a)) * t),
                   uint8_t(b8(a) + (b8(b) - b8(a)) * t));
}
static uint8_t A(float f) { return f <= 0 ? 0 : f >= 1 ? 255 : uint8_t(f * 255 + 0.5f); }
static float ease(float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }

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

static float swingAngle(float p, bool working) {
    if (!working) return 70;
    if (p < STRIKE0) return 18 - 143 * ease(p / STRIKE0);
    if (p < STRIKE1) return -125 + 143 * ((p - STRIKE0) / (STRIKE1 - STRIKE0));
    return 18;
}

static void coin(Canvas& c, const Theme& t, float x, float y, float r, float a = 1) {
    c.setOpacity(A(a));
    c.glow(x, y, r * 2.4f, t.sun, A(0.45f * a));
    c.fillCircle(x, y, r, t.sun2);
    c.fillCircle(x - r * 0.12f, y - r * 0.12f, r * 0.86f, t.sun);
    c.ring(x, y, r * 0.62f, 0.9f, t.sun2);
    if (r >= 4.5f) c.text(int16_t(x), int16_t(y + r * 0.42f), "D", FONT_L_MICRO, t.sun2, Align::Center);
    c.fillCircle(x - r * 0.45f, y - r * 0.5f, r * 0.22f, SHINE);
    c.setOpacity(255);
}

static void pickaxe(Canvas& c, float ang, uint8_t alpha) {
    const float a = ang * DEG, dx = cosf(a), dy = sinf(a);
    const float tx = SHX + dx * HANDLE, ty = SHY + dy * HANDLE, px = -dy, py = dx;
    c.setOpacity(alpha);
    c.line(SHX, SHY, tx, ty, 2.8f, WOOD);
    c.line(SHX + dx * 2, SHY + dy * 2 - 0.6f, tx - dx * 2, ty - dy * 2 - 0.6f, 0.9f, WOOD_HI);
    c.line(tx - px * 10 - dx * 2.5f, ty - py * 10 - dy * 2.5f, tx + px * 8, ty + py * 8, 3.6f, METAL_D);
    c.line(tx - px * 9 - dx * 2.5f, ty - py * 9 - dy * 2.5f - 0.7f, tx + px * 7, ty + py * 7 - 0.7f, 1.4f, METAL);
    c.setOpacity(255);
}

// "BTC block today: 1 in 398.0 trillion"
static void lotteryText(char* b, size_t n, float ducoHr, float btcHr) {
    const double ours = ducoHr / 3.0;    // a Bitcoin hash is ~3x the work of one DUCO-S1 hash
    if (ours <= 0 || btcHr <= 0) { snprintf_P(b, n, PSTR("Bitcoin odds: waiting for data")); return; }
    static const char W[] PROGMEM = "\0           thousand\0   million\0    billion\0    trillion\0   "
                                    "quadrillion\0quintillion\0sextillion\0 septillion";
    double v = btcHr / (ours * 144.0);
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
    L[0] = {110, 100, 120, t.sun, uint8_t(t.dark ? 26 : 30)};       // warm lamp light in the mine
    L[1] = {230, 10, 110, t.accent, uint8_t(t.dark ? 90 : 70)};
    L[2] = {10, 230, 90, t.accent2, uint8_t(t.dark ? 50 : 40)};
    paintBackground(c, t, L, 3);
}

void MiningScreen::enter(uint32_t now) {
    minuteTick(lastMin_);
    ver_ = g_state.mine.version;
    lastFrame_ = cycleFrom_ = now;
    ticker_ = 255;
}

void MiningScreen::update(uint32_t now, DirtySet& d) {
    const MiningData& m = g_state.mine;
    if (minuteTick(lastMin_)) d.add(HEADER_TIME_RECT);
    if (m.version != ver_) { ver_ = m.version; d.add(R_CARD); d.add(R_HUD); d.add(R_BUBBLE); d.add(R_CART); }
    const uint8_t tk = uint8_t((now / 5000) & 1);                    // ticker alternates every 5 s
    if (tk != ticker_) { ticker_ = tk; d.add(Rect(18, 218, 210, 18)); }
    if (now - lastFrame_ < 250) return;                              // 4 fps: drawing must leave time to mine
    lastFrame_ = now;
    const bool working = m.status == MineStatus::Mining;
    float hr = m.hr > 40000 ? 40000 : m.hr;
    const float period = 1400 - hr / 40000.f * 650;                  // faster swing with the hash rate
    swing_ = working ? fmodf((now - cycleFrom_) / period, 1.f) : 0;
    if (!working) cycleFrom_ = now;
    static uint8_t tick = 0;
    tick++;
    if (working) {
        d.add(R_SWING);                                              // swing, blink, beam
        const bool impact = swing_ >= STRIKE1 && swing_ < 0.97f;
        if (impact) d.add(R_SPARK);
        if (impact || (tick & 3) == 0) d.add(R_ORE);                 // flash on impact, shimmer ~1x a second
    } else {
        d.add(R_ZZZ);
        if ((tick & 3) == 0) d.add(R_ORE);
    }
    const bool coinNow = m.lastShareAt && m.lastGood && now - m.lastShareAt < 900;
    if (coinNow || coinWas_) d.add(R_COIN);
    coinWas_ = coinNow;
    const bool cartNow = m.lastShareAt && m.lastGood && now - m.lastShareAt >= 900 && now - m.lastShareAt < 1700;
    if (cartNow || cartWas_) d.add(R_CART);
    cartWas_ = cartNow;
    const bool bubbleNow = m.lastShareAt && !m.lastGood && now - m.lastShareAt < 1500;
    if (bubbleNow != bubbleWas_) d.add(R_BUBBLE);
    bubbleWas_ = bubbleNow;
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
    const bool work = m.status == MineStatus::Mining;
    const float p = swing_;
    const uint16_t cave = t.dark ? mix(t.bg, BLACK, 0.30f) : mix(t.bg, t.text, 0.10f);
    const uint16_t floorC = t.dark ? mix(t.bg, t.text, 0.06f) : mix(t.bg, t.text, 0.16f);
    const uint16_t floorHi = t.dark ? mix(t.bg, t.text, 0.16f) : mix(t.bg, WHITE, 0.6f);

    // ---- cave: ceiling rocks, stalactites, lantern, floor, rails
    c.setOpacity(128);                                             // ceiling rocks stay below the header (y >= 30)
    for (int i = 0, x = -6; x < 250; i++, x += 20) c.fillCircle(x, 41 + (i % 3) * 2, 9 + (i % 2) * 2, cave);
    c.setOpacity(255);
    static const uint8_t ST[3][2] = {{34, 9}, {98, 6}, {166, 8}};
    for (const auto& s : ST) {
        c.line(s[0], 38, s[0], 38 + s[1], 3.2f, cave);
        c.line(s[0], 38 + s[1], s[0], 41 + s[1], 1.6f, cave);
    }
    c.line(30, 34, 30, 48, 1, METAL_D);
    c.glow(30, 55, 22, t.sun, A(0.38f));
    c.fillRoundRect(26, 48, 8, 3, 1, METAL_D);
    c.fillRoundRect(26.5f, 51, 7, 8, 2, LANTERN);
    c.fillRoundRect(26, 58.5f, 8, 2.5f, 1, METAL_D);
    c.fillRect(0, int16_t(GROUND), 240, 14, floorC);
    c.setOpacity(204); c.fillRect(0, int16_t(GROUND), 240, 1, floorHi); c.setOpacity(255);
    for (int x = 4; x < 240; x += 13) c.fillRoundRect(x, GROUND + 1.5f, 7, 3, 1, WOOD_D);
    c.line(0, GROUND + 0.5f, 240, GROUND + 0.5f, 1.6f, METAL_D);

    // ---- the ore
    const float rx = ROCKX, ry = ROCKY;
    c.fillCircle(rx + 16, ry + 8, 12, STONE_D);
    c.fillCircle(rx, ry, 18, STONE);
    c.fillCircle(rx - 12, ry + 9, 10, STONE);
    c.fillRect(int16_t(rx - 20), int16_t(ry + 4), 46, int16_t(GROUND - ry - 4), STONE);
    c.setOpacity(140); c.fillCircle(rx - 5, ry - 7, 9, STONE_HI); c.setOpacity(255);
    c.setOpacity(128); c.fillRect(int16_t(rx - 20), int16_t(GROUND - 4), 46, 4, STONE_D); c.setOpacity(255);
    const float shimmer = 0.5f + 0.5f * sinf(now / 650.f);
    float flash = 0;
    if (work && p >= STRIKE1 && p < 0.95f) flash = 1 - (p - STRIKE1) / (0.95f - STRIKE1);
    c.glow(rx + 4, ry - 6, 22, t.highlight, A(0.25f + 0.15f * shimmer + 0.5f * flash));
    static const int8_t SH[4][5] = {{-5, -6, 14, -102, 50}, {3, -5, 20, -74, 64}, {12, -1, 12, -48, 46}, {-12, -1, 8, -128, 38}};
    for (const auto& g : SH) {
        const float bx = rx + g[0], by = ry + g[1], ln = g[2], w = g[4] / 10.f;
        const float dx = cosf(g[3] * DEG), dy = sinf(g[3] * DEG), px = -dy, py = dx;
        const float ex = bx + dx * ln, ey = by + dy * ln;
        c.line(bx - px * w * 0.22f, by - py * w * 0.22f, ex - px * 0.6f, ey - py * 0.6f, w * 0.55f, t.accent2);
        c.line(bx + px * w * 0.22f, by + py * w * 0.22f, ex + px * 0.4f, ey + py * 0.4f, w * 0.5f, t.highlight);
        c.line(ex - dx * 3, ey - dy * 3, ex, ey, w * 0.35f, WHITE);
    }
    static const int8_t TW[3][2] = {{22, -22}, {-14, -18}, {8, -27}};
    for (int i = 0; i < 3; i++) {                                    // twinkles
        const float k = 0.5f + 0.5f * sinf(now / 380.f + i * 2.1f);
        if (k <= 0.55f) continue;
        const float a = (k - 0.55f) / 0.45f, L = 1.5f + 2.5f * a, sx = rx + TW[i][0], sy = ry + TW[i][1];
        c.glow(sx, sy, 5, t.highlight, A(0.5f * a));
        c.setOpacity(A(a));
        c.line(sx - L, sy, sx + L, sy, 0.8f, WHITE);
        c.line(sx, sy - L, sx, sy + L, 0.8f, WHITE);
        c.fillCircle(sx, sy, 0.9f, WHITE);
        c.setOpacity(255);
    }

    // ---- mine cart (fills up over the session)
    const uint32_t land = m.lastShareAt + 900;
    float bounce = 0;
    if (m.lastShareAt && m.lastGood && now - land < 260) bounce = 2.2f * sinf(PI_F * (now - land) / 260.f);
    const float cx = CARTX, cy = CARTY + bounce;
    const uint32_t pile = m.acc / 25 + 1 > 5 ? 5 : m.acc / 25 + 1;
    for (uint32_t i = 0; i < pile; i++) coin(c, t, cx + 10 + (i * 7) % 28, cy - 1 - (i / 4) * 4, 4.2f);
    c.fillRoundRect(cx, cy, 46, 17, 3, CART);
    c.fillRoundRect(cx + 2, cy + 2, 42, 4, 2, CART_HI);
    c.fillRect(int16_t(cx - 2), int16_t(cy - 1), 50, 3, METAL_D);
    c.setOpacity(179); c.fillRect(int16_t(cx - 2), int16_t(cy - 1), 50, 1, METAL); c.setOpacity(255);
    for (float wx : {cx + 10, cx + 36}) {
        c.fillCircle(wx, GROUND - 4, 4.6f, BOOT);
        c.ring(wx, GROUND - 4, 4.6f, 1.2f, METAL_D);
        c.fillCircle(wx, GROUND - 4, 1.4f, METAL);
    }
    if (m.lastShareAt && m.lastGood && now - land < 800) {
        const float k = (now - land) / 800.f;
        c.setOpacity(A(1 - k));
        c.text(int16_t(cx + 23), int16_t(cy - 10 - 16 * k), "+1", FONT_L_NUM_S, t.sun, Align::Center);
        c.setOpacity(255);
    }

    // ---- the miner
    const float sit = work ? 0 : 10;
    float headY = 84 + ((work && p >= STRIKE0 && p < 0.9f) ? 1 : 0);
    c.line(60, 112 + sit, 56 - (sit ? 8 : 0), GROUND - 2, 4.2f, BOOT);
    c.line(66, 112 + sit, 70 + (sit ? 10 : 0), GROUND - 2, 4.2f, BOOT);
    c.fillRoundRect(50 - (sit ? 8 : 0), GROUND - 4, 10, 4, 2, BOOT);
    c.fillRoundRect(66 + (sit ? 10 : 0), GROUND - 4, 10, 4, 2, BOOT);
    c.line(58, 99 + sit, 51, 112 + sit, 4.2f, mix(t.accent, BLACK, 0.25f));
    const float by = 92 + sit;
    c.fillRoundRect(53, by, 21, 25, 7, t.accent);
    c.setOpacity(71); c.fillRoundRect(65, by + 2, 9, 22, 5, BLACK); c.setOpacity(255);
    c.line(57, by + 2, 57, by + 12, 1.6f, t.accent2);
    c.line(70, by + 2, 70, by + 12, 1.6f, t.accent2);
    c.fillRoundRect(53, by + 16, 21, 4, 1.5f, mix(t.accent2, BLACK, 0.3f));
    c.fillRoundRect(61, by + 16, 5, 4, 1, t.sun);
    const float hy = headY + sit;
    c.fillCircle(64, hy, 8.6f, SKIN);
    c.setOpacity(89); c.fillCircle(67.5f, hy + 1, 7.2f, SKIN_SH); c.setOpacity(255);
    c.fillCircle(64.5f, hy - 0.6f, 7.4f, SKIN);
    c.setOpacity(115); c.fillCircle(69, hy + 3.4f, 2.2f, BLUSH); c.setOpacity(255);
    const bool blink = !work || (now % 3700) < 130;
    if (blink) c.line(66.5f, hy + 0.6f, 70, hy + 0.6f, 1.2f, INK);
    else c.fillCircle(68.4f, hy + 0.2f, 1.35f, INK);
    c.fillCircle(64, hy - 5, 9.6f, t.sun);                          // helmet
    c.fillRoundRect(53, hy - 3, 24, 3.4f, 1.7f, mix(t.sun, BLACK, 0.18f));
    c.setOpacity(153); c.fillCircle(60, hy - 10, 2.6f, HELMET_HI); c.setOpacity(255);
    c.fillRoundRect(70, hy - 9, 6, 6, 2, METAL_D);
    c.fillCircle(74.5f, hy - 6, 2.3f, WARM);
    if (work) {                                                      // lamp beam towards the ore
        for (int i = 0; i < 3; i++) {                                // 3 soft spots (each glow costs per pixel)
            const float k = i / 2.f;
            c.glow(76 + (HITX + 6 - 76) * k, hy - 6 + (HITY - 4 - hy + 6) * k, 5 + 12 * k, BEAM, A(0.62f - 0.38f * k));
        }
    }
    if (work && p >= STRIKE0 && p < STRIKE1) {                       // motion blur
        const float a = swingAngle(p, true);
        pickaxe(c, a - 34, 46);
        pickaxe(c, a - 17, 97);
    }
    const float ang = work ? swingAngle(p, true) : 62;
    pickaxe(c, ang, 255);
    const float hx = SHX + cosf(ang * DEG) * 12, hy2 = SHY + sinf(ang * DEG) * 12 + (work ? 0 : sit);
    c.line(SHX - 2, SHY + sit, hx, hy2, 4.4f, t.accent);
    c.fillCircle(hx, hy2, 3, SKIN);

    // ---- impact: sparks and rock chips
    if (work && p >= STRIKE1 && p < 0.95f) {
        const float k = (p - STRIKE1) / (0.95f - STRIKE1);
        c.setOpacity(A(1 - k));
        for (int i = 0; i < 7; i++) {
            const float a = (150 + i * 20) * DEG, r = 3 + 15 * k;
            const float sx = HITX + cosf(a) * r, sy = HITY + sinf(a) * r - 5 * k;
            c.line(sx, sy, sx - cosf(a) * 3, sy - sinf(a) * 3, 1.3f, i % 2 ? t.sun : WARM);
        }
        static const int8_t CH[3][2] = {{-9, -16}, {-15, -9}, {-4, -20}};
        for (const auto& v : CH)
            c.fillRect(int16_t(HITX + v[0] * k * 1.6f), int16_t(HITY + v[1] * k + 30 * k * k), 2, 2, STONE_HI);
        c.setOpacity(255);
    }

    // ---- an accepted share: a coin with a trail flies from the ore into the cart
    if (m.lastShareAt && m.lastGood && now - m.lastShareAt < 900) {
        const float ft = float(now - m.lastShareAt);
        static const uint8_t LAG[3] = {120, 60, 0};
        for (int j = 0; j < 3; j++) {
            float k = (ft - LAG[j]) / 900.f;
            if (k < 0) k = 0;
            const float x = FROMX + (TOX - FROMX) * k, y = FROMY + (TOY - FROMY) * k - 38 * sinf(PI_F * k);
            coin(c, t, x, y, j == 2 ? 5.2f : 4.0f, j == 2 ? 1.0f : 0.18f + 0.2f * j);
        }
    }

    // ---- speech bubble and Zzz
    const char* say = nullptr;
    const bool red = m.lastShareAt && !m.lastGood && now - m.lastShareAt < 1500;
    if (red) say = "Oops - bad share";
    else if (!work) say = m.status == MineStatus::WaitingPool ? "Finding a pool..." :
                          m.status == MineStatus::Connecting ? "Connecting..." :
                          m.why[0] ? m.why : "Taking a break";
    if (say) {
        // the bubble ends before the DUCO pill (which starts at x 130 or later)
        char fitSay[32];
        Canvas::ellipsize(say, FONT_L_MICRO, 92, fitSay, sizeof fitSay);
        const float w = Canvas::textWidth(fitSay, FONT_L_MICRO) + 16, bx = 16, byy = 40;
        const uint16_t col = red ? t.bad : t.surfaceHi;
        c.fillRoundRect(bx, byy, w, 17, 8.5f, col);
        c.line(bx + 40, byy + 16, bx + 44, byy + 23, 2.6f, col);
        c.text(int16_t(bx + 8), int16_t(byy + 12), fitSay, FONT_L_MICRO, red ? WHITE : t.text);
    }
    if (!work) {
        for (int i = 0; i < 3; i++) {
            const float k = fmodf(now / 1400.f + i / 3.f, 1.f);
            c.setOpacity(A(1 - k));
            c.text(int16_t(80 + 10 * k + i * 4), int16_t(88 - 18 * k - i * 3), i < 2 ? "z" : "Z",
                   i < 2 ? FONT_L_MICRO : FONT_L_LABEL, t.text2);
            c.setOpacity(255);
        }
    }

    // ---- HUD pill: DUCO earned today
    snprintf_P(buf, sizeof buf, PSTR("%.4f"), m.relayOk ? m.today : 0.f);
    const float pw = Canvas::textWidth(buf, FONT_L_NUM_S) + 30, px0 = 226 - pw;
    c.setOpacity(191); c.fillRoundRect(px0, 37, pw, 20, 10, t.surface); c.setOpacity(255);
    c.strokeRoundRect(px0, 37, pw, 20, 10, 1, t.stroke);
    coin(c, t, px0 + 11, 47, 5.2f);
    c.text(int16_t(px0 + 21), 53, buf, FONT_L_NUM_S, t.text);

    // ---- glass stats card
    glassCard(c, t, 10, 150, 220, 86, 16);
    label(c, 22, 167, FS("HASH RATE"), t.text3);
    const char* unit = "H/S";
    if (m.hr >= 1000) { snprintf_P(buf, sizeof buf, PSTR("%.2f"), m.hr / 1000.f); unit = "KH/S"; }
    else snprintf_P(buf, sizeof buf, PSTR("%.0f"), m.hr);
    const int16_t w = c.textGradient(20, 198, buf, FONT_L_NUM_L, t.clockTop, t.clockBot);
    label(c, int16_t(20 + w + 5), 198, unit, t.text3);
    const uint32_t tot = m.acc + m.rej;
    const float pct = tot ? float(m.acc) / tot : 0;
    // acceptance ring: the number alone fits inside (up to "100", 29 px in 31 px); "% GOOD" sits beside it
    const float gx = 205, gy = 180;
    c.ring(gx, gy, 19, 3.4f, t.surfaceHi);
    if (pct > 0) c.arcGradient(gx, gy, 19, 3.4f, 270, 270 + 360 * pct, t.good, t.highlight);
    snprintf_P(buf, sizeof buf, PSTR("%u"), unsigned(pct * 100 + 0.5f));
    c.text(int16_t(gx), int16_t(gy + 5), buf, FONT_L_NUM_S, t.text, Align::Center);
    c.text(int16_t(gx - 23), int16_t(gy + 4), "% GOOD", FONT_L_MICRO, t.text3, Align::Right);
    // share counts, right-aligned to the card's inner edge (the sparkline stops short of them)
    snprintf_P(buf, sizeof buf, PSTR("%lu OK  %lu BAD"), (unsigned long)m.acc, (unsigned long)m.rej);
    c.text(224, 213, buf, FONT_L_MICRO, t.text3, Align::Right);
    // filled sparkline: hourly averages + now
    const uint8_t n = m.histN;
    float mx = m.hr > 1 ? m.hr : 1;
    for (uint8_t i = 0; i < n; i++) if (m.hist[i] > mx) mx = m.hist[i];
    const float x0 = 22, x1 = 136, yb = 218, hh = 13;
    const uint8_t pts = n + 1;
    float lx = x0, ly = yb;
    for (uint8_t i = 0; i < pts; i++) {
        const float v = i < n ? m.hist[i] : m.hr;
        const float x = pts > 1 ? x0 + (x1 - x0) * i / float(pts - 1) : x1, y = yb - hh * v / mx;
        if (i) {
            c.setOpacity(56);
            for (int xx = int(lx); xx <= int(x); xx++) {
                const float k = (xx - lx) / (x - lx > 1 ? x - lx : 1), yy = ly + (y - ly) * k;
                c.fillRect(int16_t(xx), int16_t(yy), 1, int16_t(yb - yy), t.highlight);
            }
            c.setOpacity(255);
            c.line(lx, ly, x, y, 1.5f, t.highlight);
        }
        lx = x; ly = y;
    }
    c.glow(lx, ly, 6, t.highlight, 128);
    c.fillCircle(lx, ly, 2.2f, WHITE);
    // ticker: earnings / Bitcoin odds, every 5 s
    if (((now / 5000) & 1) == 0) {
        if (m.relayOk && m.inr > 0) snprintf_P(buf, sizeof buf, PSTR("INR %.6f today  \xC2\xB7  %.2f total"), m.today * m.inr, m.bal);
        else if (m.relayOk) snprintf_P(buf, sizeof buf, PSTR("%.4f DUCO today  \xC2\xB7  %.2f total"), m.today, m.bal);
        else snprintf_P(buf, sizeof buf, PSTR("Earnings: waiting for the relay"));
    } else {
        lotteryText(buf, sizeof buf, m.hr, m.btcHr);
    }
    char fit[48];
    Canvas::ellipsize(buf, FONT_L_BODY, 200, fit, sizeof fit);
    c.text(22, 231, fit, FONT_L_BODY, t.text2);
}
