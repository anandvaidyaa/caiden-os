#include "SystemScreens.h"
#include <math.h>
#include <stdio.h>
#include "../ui/Widgets.h"
#include "../state/AppState.h"

using namespace ui;

void BootScreen::render(Canvas& c, const Theme& t) {
    c.fillCircle(120, 104, 5, t.accent);
    c.text(120, 140, "CAIDEN", FONT_TITLE, t.text, Align::Center, 4);
    label(c, 120, 160, "OS " CAIDEN_FW_VERSION, t.text2, Align::Center);
}

static const Rect R_UPDATE(0, 60, 240, 170);

void UpdateScreen::update(uint32_t, DirtySet& d) {
    const OtaState& o = g_state.ota;
    if (o.pct != shownPct_ || uint8_t(o.phase) != shownPhase_) {
        shownPct_ = o.pct;
        shownPhase_ = uint8_t(o.phase);
        d.add(R_UPDATE);
    }
}

void UpdateScreen::render(Canvas& c, const Theme& t) {
    const OtaState& o = g_state.ota;
    const bool failed = o.phase == OtaPhase::Failed;
    const uint16_t col = failed ? t.bad : t.accent;
    label(c, 120, 40, "SYSTEM UPDATE", t.text2, Align::Center);
    const float cx = 120, cy = 124, r = 52, th = 6;
    c.setOpacity(50);
    c.ring(cx, cy, r, th, t.text2);
    c.setOpacity(255);
    const float sweep = 360.f * (failed ? 100 : o.pct) / 100.f;
    if (sweep > 0.5f) c.arc(cx, cy, r, th, 270, 270 + sweep, col);   // from 12 o'clock, clockwise
    char buf[8];
    if (failed) snprintf(buf, sizeof buf, "!");
    else snprintf(buf, sizeof buf, "%u%%", unsigned(o.pct));
    c.text(120, cy + 12, buf, FONT_NUM28, t.text, Align::Center);
    const char* l1 = failed ? "Update failed" : o.phase == OtaPhase::Restarting ? "Restarting" : "Receiving firmware";
    c.text(120, 204, l1, FONT_BODYM, failed ? t.bad : t.text, Align::Center);
    label(c, 120, 224, failed ? o.err : "Keep the clock powered", t.text2, Align::Center);
}


static const Rect R_RINGS(60, 36, 120, 84);

void SetupScreen::update(uint32_t now, DirtySet& d) {
    if (now - last_ >= 80) {          // ~12 fps for the radiating rings only
        last_ = now;
        pulse_ = ((now - start_) % 2400) / 2400.f;
        d.add(R_RINGS);
    }
}

void SetupScreen::render(Canvas& c, const Theme& t) {
    label(c, M, 22, "SETUP", t.text2);
    // radiating Wi-Fi arcs from a source dot
    const float cx = 120, cy = 104;
    for (int i = 0; i < 3; i++) {
        float k = fmodf(pulse_ + i / 3.f, 1.f);
        float r = 14 + k * 46;
        uint8_t a = uint8_t(255 * (1 - k) * (1 - k));
        c.setOpacity(a);
        c.arc(cx, cy, r, 3, 225, 315, t.accent);
        c.setOpacity(255);
    }
    c.fillCircle(cx, cy, 5, t.text);

    c.text(120, 144, "Join CAIDEN to set up", FONT_TITLE, t.text, Align::Center);
    // credentials block
    label(c, M + 6, 172, "WI-FI", t.text2);
    c.text(W - M - 6, 172, g_state.net.apName, FONT_BODYM, t.text, Align::Right);
    label(c, M + 6, 194, "PASSWORD", t.text2);
    c.text(W - M - 6, 194, g_state.net.apPass, FONT_MONO, t.text, Align::Right);
    label(c, M + 6, 216, "THEN OPEN", t.text2);
    c.text(W - M - 6, 216, "192.168.4.1", FONT_MONO, t.accent, Align::Right);
}
