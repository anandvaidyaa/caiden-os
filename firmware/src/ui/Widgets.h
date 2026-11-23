#pragma once
// CAIDEN OS component library (DESIGN_SYSTEM.md section 4).
// Stateless draw functions: every screen composes these so the visual language stays consistent.
#include "../graphics/Canvas.h"
#include "../graphics/fonts/Fonts.h"
#include "Theme.h"

namespace ui {

constexpr int16_t M = 14;            // outer margin
constexpr int16_t W = 240;
constexpr int8_t LABEL_TRACK = 1;    // ~1.4 px tracking, rounded for the integer pen

enum class Sky : uint8_t { Clear, Partly, Cloudy, Fog, Drizzle, Rain, Snow, Storm, Unknown };

// Uppercase tracked label. Text should already be uppercase.
int16_t label(Canvas& c, int16_t x, int16_t y, const char* s, uint16_t col, Align a = Align::Left);

// Filled card (no stroke).
void card(Canvas& c, const Theme& t, float x, float y, float w, float h, float r);

// Progress bar. yc = vertical centre.
void bar(Canvas& c, const Theme& t, float x, float yc, float w, float frac, float h, uint16_t fill);

// Home AQI ring gauge.
void ringGauge(Canvas& c, const Theme& t, float cx, float cy, float r, float thick, float frac, uint16_t col);

// Procedural weather icon centred at (cx, cy); `phase` 0..1 drives the animation loop.
void weatherIcon(Canvas& c, const Theme& t, Sky kind, float cx, float cy, float size, float phase, bool night);


// Status bar for secondary pages: title + live time (+ offline dot).
void statusBar(Canvas& c, const Theme& t, const char* title, const char* timeStr, bool offline);

// Single-line marquee clipped to [x, x+w). Text that fits is drawn still; longer text holds
// 2 s, scrolls at 30 px/s with a 24 px gap and loops (DESIGN_SYSTEM.md 6). `elapsedMs` = time
// since the text was shown. Returns true when the text is scrolling (caller keeps it dirty).
bool marquee(Canvas& c, int16_t x, int16_t y, int16_t w, const char* s, const Font& f, uint16_t col, uint32_t elapsedMs);

// Transient page indicator.
void pageDots(Canvas& c, const Theme& t, uint8_t index, uint8_t count);

Sky skyFromWmo(int code);
const char* skyName(int code);

}  // namespace ui
