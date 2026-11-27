#include "IdleScreen.h"
#include <math.h>
#include "../ui/Widgets.h"
#include "../services/ClockService.h"
#include "../configuration/ConfigurationManager.h"

using namespace ui;

static const float CX = 120, CY = 120, ORBIT_R = 100;

static void orbitPoint(int sec, float& x, float& y) {
    float a = (-90.f + 6.f * sec) * 0.0174533f;
    x = CX + ORBIT_R * cosf(a);
    y = CY + ORBIT_R * sinf(a);
}

Rect IdleScreen::dotRect(int sec) const {
    float x, y; orbitPoint(sec, x, y);
    return Rect(int16_t(x + ox_ - 6), int16_t(y + oy_ - 6), 12, 12);
}

void IdleScreen::enter(uint32_t now) {
    (void)now;
    struct tm t;
    ox_ = int8_t(ESP.random() % 9) - 4;
    oy_ = int8_t(ESP.random() % 9) - 4;
    if (g_clock.now(t)) { lastMin_ = t.tm_min; lastSec_ = t.tm_sec; }
    else lastSec_ = lastMin_ = -1;
}

void IdleScreen::update(uint32_t now, DirtySet& d) {
    (void)now;
    struct tm t;
    if (!g_clock.now(t)) return;
    if (t.tm_min != lastMin_) {
        lastMin_ = t.tm_min;
        ox_ = int8_t(ESP.random() % 9) - 4;      // pixel shift +/-4 px, new each minute
        oy_ = int8_t(ESP.random() % 9) - 4;
        lastSec_ = t.tm_sec;
        d.addAll();
        return;
    }
    if (t.tm_sec != lastSec_) {
        if (lastSec_ >= 0) d.add(dotRect(lastSec_));
        lastSec_ = t.tm_sec;
        d.add(dotRect(lastSec_));
    }
}

void IdleScreen::render(Canvas& c, const Theme& t) {
    c.setOffset(ox_, oy_);
    const uint16_t orbit = mix565(t.bg, t.text, 56);         // text at 22 % over bg
    for (int i = 0; i < 120; i++) {
        float a = i * 3.f * 0.0174533f;
        c.fillCircle(CX + ORBIT_R * cosf(a), CY + ORBIT_R * sinf(a), 0.7f, orbit);
    }
    if (lastSec_ >= 0) {
        float x, y; orbitPoint(lastSec_, x, y);
        c.fillCircle(x, y, 3.5f, t.accent);
    }
    char hh[4], mm[4]; bool am;
    Config& cfg = g_config.get();
    g_clock.hhmm(hh, mm, cfg.h24, am);
    char s[8]; snprintf_P(s, sizeof s, PSTR("%s:%s"), hh, mm);
    const uint16_t timeCol = mix565(t.bg, t.text, 158);      // 62 %
    c.text(120, 142, s, FONT_NUM64, timeCol, Align::Center);
    char date[24];
    g_clock.dateLabel(date, sizeof date);
    label(c, 120, 170, date, mix565(t.bg, t.text2, 204), Align::Center);   // 80 %
    c.setOffset(0, 0);
}
