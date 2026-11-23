#include "Theme.h"
#include <string.h>
#include <math.h>

#define C(x) rgb(0x##x)

// Values copied from design2/caiden_ui.py THEMES (the approved renders).
static const Theme THEMES[] = {
    {"midnight", "Midnight Luxe", true,
     C(050A1C), C(0B1838), C(0F1B3D), C(1B2C5E), C(2A3D78),
     C(F3F6FF), C(A7B5DA), C(6A7BA8),
     C(3D7BFF), C(8A6BFF), C(3FE0FF),
     C(38E0A0), C(FFC24B), C(FF5C7A), C(38E0A0), C(FF6B8A),
     {C(3BDC97), C(A6E35A), C(FFD24A), C(FF9A47), C(FF5470), C(C2367A)},
     C(FFFFFF), C(B9CCFF), C(3FE0FF),
     C(0B1A4A), C(24408E),
     C(FFD15C), C(FF9E3D), C(E6ECFF), C(EAF0FF), C(9DAEDB), C(7C8DBF), C(4FD6FF), C(8E9DC6),
     2, {{196, 30, 150, C(1E3FA8), 120}, {20, 230, 140, C(3A1F8C), 90}}, 158, C(FFFFFF), 56},
    {"aurora", "Aurora", true,
     C(08061A), C(1A1142), C(1A1640), C(2A2463), C(3B3283),
     C(F6F3FF), C(BEB5EA), C(7F77B2),
     C(9A6BFF), C(2EE6D6), C(7AF5E3),
     C(3EEBB0), C(FFC857), C(FF5E8A), C(3EEBB0), C(FF6FA0),
     {C(3EEBB0), C(B5EC5C), C(FFD45E), C(FFA057), C(FF5E8A), C(C73C96)},
     C(FFFFFF), C(CDBEFF), C(2EE6D6),
     C(140C3E), C(3A2C8F),
     C(FFD36B), C(FF8F5A), C(F0EBFF), C(F3EEFF), C(A99BDB), C(8475BE), C(5CF0E0), C(9A8FC9),
     2, {{40, 20, 150, C(5A2DC8), 110}, {220, 110, 130, C(0E8C9C), 80}}, 148, C(FFFFFF), 62},
    {"crystal", "Crystal", false,
     C(EEF2FB), C(DCE5F7), C(FFFFFF), C(F5F8FF), C(C6D2EC),
     C(0D1530), C(3F4C74), C(67749A),
     C(2B5BF0), C(7B4DFF), C(0AA7CC),
     C(0E9F6E), C(C77A00), C(DB2F55), C(0E9F6E), C(DB2F55),
     {C(12A574), C(6E9E12), C(C99300), C(E0701C), C(D9334E), C(9B1F5E)},
     C(0B1430), C(2E4A9E), C(2B5BF0),
     C(8FB4FF), C(DDE8FF),
     C(FFC23D), C(FF8A1F), C(3A4C86), C(FFFFFF), C(B9C7E6), C(8C9BC2), C(1F8FE0), C(8A98BE),
     2, {{200, 20, 150, C(B9CCFF), 150}, {10, 230, 140, C(D9C9FF), 130}}, 199, C(FFFFFF), 230},
    {"hypertech", "Hypertech", true,
     C(02050B), C(061220), C(0A1626), C(12263D), C(1C3956),
     C(E9FBFF), C(8DB3C6), C(55788A),
     C(00E1FF), C(6F5BFF), C(4DFFC3),
     C(4DFFC3), C(FFC23D), C(FF4D6D), C(4DFFC3), C(FF4D6D),
     {C(4DFFC3), C(B8F25A), C(FFD23D), C(FF9440), C(FF4D6D), C(D13A8F)},
     C(FFFFFF), C(9FEFFF), C(00E1FF),
     C(041A2E), C(0B3A5A),
     C(FFD45C), C(FF9A3C), C(DDF6FF), C(E4F6FF), C(86A9BF), C(5F8199), C(00E1FF), C(6D8FA3),
     2, {{120, 120, 130, C(063A55), 120}, {230, 10, 90, C(2A1F7A), 90}}, 168, C(9FEFFF), 56},
};

const Theme& theme(ThemeId id) {
    uint8_t i = uint8_t(id);
    return THEMES[i < uint8_t(ThemeId::Count) ? i : 0];
}

ThemeId themeFromName(const char* name) {
    for (uint8_t i = 0; i < uint8_t(ThemeId::Count); i++)
        if (name && strcmp(name, THEMES[i].name) == 0) return ThemeId(i);
    return ThemeId::Midnight;
}

static const int AQI_BP[7] = {0, 50, 100, 200, 300, 400, 500};

uint8_t aqiBand(int aqi) {
    for (uint8_t i = 0; i < 6; i++) if (aqi <= AQI_BP[i + 1]) return i;
    return 5;
}
uint16_t aqiColor(const Theme& t, uint8_t band) { return t.aqi[band > 5 ? 5 : band]; }
const char* aqiShortName(uint8_t b) {
    static const char* N[6] = {"GOOD", "SATISFACTORY", "MODERATE", "POOR", "VERY POOR", "SEVERE"};
    return N[b > 5 ? 5 : b];
}
const char* aqiLongName(uint8_t b) {
    static const char* N[6] = {"Good", "Satisfactory", "Moderate", "Poor", "Very Poor", "Severe"};
    return N[b > 5 ? 5 : b];
}
float aqiFrac(int aqi) {
    if (aqi < 0) aqi = 0;
    if (aqi > 500) aqi = 500;
    uint8_t b = aqiBand(aqi);
    float inBand = float(aqi - AQI_BP[b]) / float(AQI_BP[b + 1] - AQI_BP[b]);
    return (b + inBand) / 6.f;
}

// ---------------------------------------------------------------- ambient cache
uint8_t g_glowPct = 100;

static uint8_t s_grid[Canvas::GRID_N * Canvas::GRID_N * 3];   // 2.9 KB, RGB888
static const Theme* s_gridTheme = nullptr;
static Light s_gridLights[6];
static uint8_t s_gridCount = 0, s_gridPct = 0;

static void buildGrid(const Theme& t, const Light* lights, uint8_t count) {
    const float br = r8(t.bg), bg = g8(t.bg), bb = b8(t.bg);
    const float tr = r8(t.bg2), tg = g8(t.bg2), tb = b8(t.bg2);
    for (int gy = 0; gy < Canvas::GRID_N; gy++) {
        for (int gx = 0; gx < Canvas::GRID_N; gx++) {
            float x = gx * 8.f, y = gy * 8.f;
            float k = y / 239.f; if (k > 1) k = 1;
            float r = br + (tr - br) * k, g = bg + (tg - bg) * k, b = bb + (tb - bb) * k;
            for (uint8_t i = 0; i < count; i++) {
                const Light& L = lights[i];
                float dx = x - L.x, dy = y - L.y;
                float f = 1.f - (dx * dx + dy * dy) / float(L.r * L.r);
                if (f <= 0) continue;
                float a = f * f * L.strength * g_glowPct / 100.f / 255.f;
                if (a > 1) a = 1;
                r += (r8(L.color) - r) * a; g += (g8(L.color) - g) * a; b += (b8(L.color) - b) * a;
            }
            uint8_t* p = &s_grid[(gy * Canvas::GRID_N + gx) * 3];
            p[0] = uint8_t(r + 0.5f); p[1] = uint8_t(g + 0.5f); p[2] = uint8_t(b + 0.5f);
        }
    }
}

void paintBackground(Canvas& c, const Theme& t, const Light* lights, uint8_t count) {
    if (!lights) { lights = t.lights; count = t.lightCount; }
    if (count > 6) count = 6;
    if (s_gridTheme != &t || s_gridCount != count || s_gridPct != g_glowPct ||
        memcmp(s_gridLights, lights, sizeof(Light) * count) != 0) {
        buildGrid(t, lights, count);
        s_gridTheme = &t; s_gridCount = count; s_gridPct = g_glowPct;
        memcpy(s_gridLights, lights, sizeof(Light) * count);
    }
    c.fillGrid(s_grid);
}
