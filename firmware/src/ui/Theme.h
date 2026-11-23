#pragma once
// CAIDEN OS 2.0 "Lumen" theme tokens - mirrors design2/DESIGN_LANGUAGE.md / design2/caiden_ui.py.
#include <stdint.h>
#include "../graphics/Canvas.h"

enum class ThemeId : uint8_t { Midnight = 0, Aurora = 1, Crystal = 2, Hypertech = 3, Count = 4 };

struct Light { int16_t x, y, r; uint16_t color; uint8_t strength; uint8_t pad = 0; };   // pad: memcmp-safe

struct Theme {
    const char* name;        // config key: midnight / aurora / crystal / hypertech
    const char* label;       // display name
    bool dark;
    uint16_t bg, bg2, surface, surfaceHi, stroke;
    uint16_t text, text2, text3;
    uint16_t accent, accent2, highlight;
    uint16_t good, warn, bad, up, down;
    uint16_t aqi[6];
    uint16_t clockTop, clockBot, colon;
    uint16_t skyTop, skyBot;
    uint16_t sun, sun2, moon, cloud, cloudShade, cloudDark, rain, fog;
    uint8_t lightCount;
    Light lights[2];
    uint8_t glassA;          // card fill opacity (0..255)
    uint16_t glassTop;       // bright top-edge colour
    uint8_t glassTopA;       // top-edge opacity (0..255)

    // v1 aliases (setup / idle / system screens until they are redesigned)
    uint16_t sub() const { return text2; }
    uint16_t faint() const { return stroke; }
};

const Theme& theme(ThemeId id);
ThemeId themeFromName(const char* name);

// Indian NAQI bands
uint8_t aqiBand(int aqi);                          // 0..5
uint16_t aqiColor(const Theme& t, uint8_t band);
const char* aqiShortName(uint8_t band);            // GOOD, SATISFACTORY, ...
const char* aqiLongName(uint8_t band);             // Good, Satisfactory, ...
float aqiFrac(int aqi);                            // equal sixths per band, 0..1

// Ambient light strength in percent (calibration; 100 = design values).
extern uint8_t g_glowPct;

// Ambient background: dithered bg->bg2 vertical gradient + soft light pools, rendered from a
// cached 31x31 grid (bilinear + ordered dither). `lights` overrides the theme's pools.
void paintBackground(Canvas& c, const Theme& t, const Light* lights = nullptr, uint8_t count = 0);
