#pragma once
// CAIDEN OS 2.0 "Lumen" component library: a 1:1 port of design2/caiden_ui.py, so the device
// renders the approved designs. Coordinates/sizes match the Python reference exactly.
#include "../graphics/Canvas.h"
#include "../graphics/fonts/Fonts.h"
#include "Theme.h"
#include "../state/AppState.h"

namespace lumen {

constexpr int16_t W = 240, M = 14;

enum class Wx : uint8_t { ClearDay, ClearNight, PartlyDay, PartlyNight, Cloudy, Rain, Storm, Fog, Snow, Unknown };
Wx wxFromWmo(int code, bool isDay);
const char* wxWord(int code, bool isDay);     // "Sunny", "Clear", "Partly cloudy", ...

// UPPERCASE label, Inter 11/600, tracking +1. y = baseline. Returns width.
int16_t label(Canvas& c, int16_t x, int16_t y, const char* s, uint16_t col, Align a = Align::Left);

// Status icons (16 px nominal box, stroke = size/9, round caps).
void iconWifi(Canvas& c, const Theme& t, float cx, float cy, float size, uint8_t bars, uint16_t col, uint16_t dim);
void iconBell(Canvas& c, float cx, float cy, float size, uint16_t col);
void badge(Canvas& c, float cx, float cy, int n, uint16_t col, uint16_t ring, bool hasRing);
float eqBars(Canvas& c, float x, float cy, float h, uint16_t col, float phase, int n = 4, float bw = 2.4f, float gap = 2.2f);

// Weather illustrations (duotone, light from top-left). s = box size. `glow` adds halos.
void weatherIcon(Canvas& c, const Theme& t, Wx kind, float cx, float cy, float s, bool glow, float phase = 0);

// Hero clock: tabular cells, vertical-gradient numerals, drawn colon (two dots).
// digitAlpha[i] (0..255) per digit for cross-fades; colonAlpha for the soft colon breathe.
struct ClockGeom { float x0, x1, colonX, colonY0, colonY1, colonR; int16_t capTop; };
ClockGeom clockLayout(const Font& f, float x, int16_t baseline, uint8_t hourDigits);
void clock(Canvas& c, const Theme& t, float x, int16_t baseline, const char* hh, const char* mm, const Font& f,
           const uint8_t* digitAlpha = nullptr, uint8_t colonAlpha = 255);

// ---- dashboards (design2/caiden_ui_ext.py)
constexpr int16_t HEADER_Y = 24, HEADER_ICON_Y = 19;
typedef void (*HeaderIcon)(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col);
// Shared page header: 14 px icon + UPPERCASE title (left), live time + Wi-Fi glyph (right).
// dataAt (unix time the page's data arrived): older than 30 min, the time gives way to "2H AGO".
void header(Canvas& c, const Theme& t, const char* title, HeaderIcon icon, uint16_t iconCol, uint8_t wifiBars,
            const char* live = nullptr, uint32_t dataAt = 0);
static const Rect HEADER_TIME_RECT(150, 8, 90, 22);     // time + Wi-Fi region (minute updates)
// Music badge: a small bouncing equaliser in every page header while Spotify plays.
// UIManager redraws only this rect (~12 fps); header() shortens the title to leave room for it.
// header badges (music equaliser, mining pickaxe): placed right to left before the time, so this rect
// covers every position they can take (the time, LIVE or a data age decides where they start)
static const Rect R_BADGES(96, 4, 96, 24);
static const Rect R_HEADER(0, 4, 240, 26);
bool musicPlaying();
bool miningShown();                                     // mining is switched on (badge visible)
bool miningActive();                                    // and hashing right now (badge animates)
// small pickaxe tapping at the ground; x = left edge, about 12 px wide; phase 0..1
void mineBadge(Canvas& c, const Theme& t, float x, float cy, bool active, float phase);
extern bool g_headerDrawn;                              // set by header(): the page has a header
// Glass surface: translucent fill + 1 px border + brighter top edge.
void glassCard(Canvas& c, const Theme& t, float x, float y, float w, float h, float r);
void iconPin(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col);
void iconLeaf(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col);
void iconDroplet(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col);
void iconWind(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col);
void iconUV(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col);
void iconUmbrella(Canvas& c, const Theme& t, float cx, float cy, float size, uint16_t col);
void dashedH(Canvas& c, float x0, float x1, float y, uint16_t col, uint8_t alpha, int dash = 2, int gap = 3);
void dashedV(Canvas& c, float x, float y0, float y1, uint16_t col, uint8_t alpha, int dash = 2, int gap = 3);
uint8_t wifiBars();
const char* compass(uint16_t deg);   // "N", "NE", ...

// ---- CAIDEN OS 3 "Atelier" (design2/render_v3.py)
// header() draws the whisper status line: accent dot + tracked page label (left), time (right).
// Story segments: n thin bars at y, past ones full, the current one `frac` filled.
static const Rect R_SEGMENTS(0, 26, 240, 8);
void segments(Canvas& c, const Theme& t, uint8_t n, uint8_t i, float frac, float y = 30, float x0 = M, float x1 = 226);
void hairline(Canvas& c, const Theme& t, float y, float x0 = M, float x1 = 226, uint8_t alpha = 140);
void degree(Canvas& c, float x, float y, float r, uint16_t col);           // drawn degree ring for hero numbers
// Hero number with the clock's vertical gradient. Returns width.
int16_t hero(Canvas& c, const Theme& t, int16_t x, int16_t y, const char* s, const Font& f, Align a = Align::Left);
// "Nothing here right now" page: soft halo rings, icon, title, hint (PROGMEM strings).
void emptyState(Canvas& c, const Theme& t, HeaderIcon icon, PGM_P title, PGM_P hint);
// Copy a PROGMEM string to b (ESP8266 keeps plain string literals in RAM).
const char* flashStr(char* b, size_t n, PGM_P p);

}  // namespace lumen
