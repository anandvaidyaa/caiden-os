#pragma once
// Idle / night screensaver (DESIGN_SYSTEM.md 5 "Idle"): flat background, dotted orbit with a
// seconds dot, dimmed time + date. Burn-in care: the whole layout shifts by 0..4 px each minute.
// Per second only two small dot regions are redrawn.
#include "../ui/Screen.h"

class IdleScreen : public Screen {
public:
    const char* name() const override { return "idle"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override { c.fill(t.bg); }   // no glow at night
private:
    Rect dotRect(int sec) const;
    int lastSec_ = -1, lastMin_ = -1;
    int8_t ox_ = 0, oy_ = 0;
};
