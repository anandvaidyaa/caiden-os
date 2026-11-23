#pragma once
// Engine benchmark scene (M3.5): the expensive building blocks of the Orbit home, drawn with
// the rewritten primitives so their real cost can be measured on the device with {"cmd":"bench"}.
//   - 24 h sun-path ring: full track r 109 + gradient daylight arc (~200 deg) + glowing sun
//   - inner ring r 93: temperature gradient arc (left) + six AQI band segments (right)
//   - 24 hour ticks, crescent moon, vertical-gradient clock digits, stroked glass card
#include "../ui/Screen.h"

class EngineTestScreen : public Screen {
public:
    const char* name() const override { return "engine"; }
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t, DirtySet&) override {}
};
