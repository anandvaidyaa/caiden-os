#include "EngineTestScreen.h"
#include <math.h>
#include "../ui/Widgets.h"

using namespace ui;

void EngineTestScreen::render(Canvas& c, const Theme& t) {
    (void)t;
    // Midnight Luxe tokens (design2/DESIGN_LANGUAGE.md)
    const uint16_t BG2 = rgb(0x0B1838), TRACK = rgb(0x1B2C5E), ACC2 = rgb(0x8A6BFF),
                   HI = rgb(0x3FE0FF), SUN = rgb(0xFFD15C), TXT = rgb(0xF3F6FF), TXT2 = rgb(0xA7B5DA);

    // 24 h sun-path ring (noon at top): track + daylight gradient arc + sun
    c.ring(120, 120, 109, 3, TRACK);
    c.arcGradient(120, 120, 109, 3, 155, 385, ACC2, HI);
    for (int h = 0; h < 24; h++) {               // hour ticks
        float a = (h * 15.f - 90.f) * 0.0174533f, r0 = h % 6 ? 103.f : 99.f;
        c.line(120 + cosf(a) * r0, 120 + sinf(a) * r0, 120 + cosf(a) * 105.f, 120 + sinf(a) * 105.f, 1.2f, TRACK);
    }
    c.glow(85, 17, 14, SUN, 200);
    c.fillCircle(85, 17, 4.5f, SUN);

    // inner ring r 93: temperature gauge (left, gradient) + six AQI band segments (right)
    c.arc(120, 120, 93, 4, 100, 260, BG2);
    c.arcGradient(120, 120, 93, 4, 115, 200, rgb(0x3FE0FF), rgb(0xFFD15C));
    static const uint32_t AQI[6] = {0x3BDC97, 0xA6E35A, 0xFFD24A, 0xFF9A47, 0xFF5470, 0xC2367A};
    for (int i = 0; i < 6; i++) {
        float a0 = -78 + i * 26.f;
        c.arc(120, 120, 93, 3, a0, a0 + 22, rgb(AQI[i]));
    }

    // gradient clock + colon dots
    c.textGradient(120, 132, "1042", FONT_CLOCK, rgb(0xFFFFFF), rgb(0xB9CCFF), Align::Center);
    c.fillCircle(120, 100, 3.5f, HI);
    c.fillCircle(120, 118, 3.5f, HI);

    // crescent + stroked glass card + label
    c.fillCrescent(200, 205, 9, 205, 201, 8, rgb(0xE6ECFF));
    c.fillRoundRect(40, 160, 160, 40, 16, rgb(0x0F1B3D));
    c.strokeRoundRect(40, 160, 160, 40, 16, 1, rgb(0x2A3D78));
    label(c, 120, 185, "ENGINE TEST", TXT2, Align::Center);
    c.text(120, 230, "M3.5", FONT_MONO, TXT, Align::Center);
}
