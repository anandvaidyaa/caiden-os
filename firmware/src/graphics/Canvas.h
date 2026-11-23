#pragma once
// CAIDEN graphics engine - band canvas.
//
// The ESP8266 cannot hold a 240x240x16bit frame (115 KB). Instead the compositor
// renders the screen in small windows ("bands") into one static buffer and pushes
// each to the panel. Every primitive is written in absolute screen coordinates and
// clips itself to the current window, so a scene can be re-rendered for any dirty
// region without the screen code knowing about bands.
//
// All shape edges are anti-aliased with analytic coverage; only edge pixels pay
// for a sqrt, interiors are span-filled.
#include <stdint.h>
#include "Color.h"
#include "Font.h"

struct Rect {
    int16_t x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(int16_t x_, int16_t y_, int16_t w_, int16_t h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool empty() const { return w <= 0 || h <= 0; }
    int16_t r() const { return x + w; }
    int16_t b() const { return y + h; }
    bool intersects(const Rect& o) const { return x < o.r() && o.x < r() && y < o.b() && o.y < b(); }
    Rect unite(const Rect& o) const;
    Rect intersect(const Rect& o) const;
};

enum class Align : uint8_t { Left, Center, Right };

// Provides one row of an image (e.g. album art streamed from LittleFS).
typedef bool (*RowSource)(void* ctx, int16_t row, uint16_t* out, int16_t x0, int16_t w);

class Canvas {
public:
    static constexpr int16_t SCREEN = 240;
    static constexpr int GRID_N = 31;          // ambient grid: 31x31 samples, 8 px pitch

    void begin(uint16_t* buf, const Rect& window) { buf_ = buf; win_ = window; }
    const Rect& window() const { return win_; }
    uint16_t* buffer() { return buf_; }

    // Translation applied to all drawing (used by slide transitions).
    void setOffset(int16_t dx, int16_t dy) { dx_ = dx; dy_ = dy; }
    // Extra clip in screen coords (intersected with the window).
    void setClip(const Rect& c) { clip_ = c; hasClip_ = true; }
    void clearClip() { hasClip_ = false; }
    bool hasClip() const { return hasClip_; }
    const Rect& clip() const { return clip_; }
    // Global opacity multiplier for fades (255 = opaque).
    void setOpacity(uint8_t o) { opacity_ = o; }

    // --- primitives (float coordinates allow sub-pixel animation)
    void fill(uint16_t c);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
    void fillRoundRect(float x, float y, float w, float h, float r, uint16_t c);
    void strokeRoundRect(float x, float y, float w, float h, float r, float t, uint16_t c);
    void fillCircle(float cx, float cy, float r, uint16_t c);
    void ring(float cx, float cy, float r, float thick, uint16_t c);   // r = outer radius
    // Arc from a0 to a1 degrees (0 = 3 o'clock, clockwise), round caps.
    void arc(float cx, float cy, float r, float thick, float a0, float a1, uint16_t c, bool caps = true);
    // Angular colour gradient c0 -> c1 along the arc (premium gauges, sun ring).
    void arcGradient(float cx, float cy, float r, float thick, float a0, float a1, uint16_t c0, uint16_t c1,
                     bool caps = true);
    // Fill the window from a GRID_N x GRID_N RGB888 grid (bilinear + ordered dither): ambient background.
    void fillGrid(const uint8_t* grid);
    // Disc minus disc (crescent moon), transparent over any background.
    void fillCrescent(float cx, float cy, float r, float bx, float by, float br, uint16_t c);
    void line(float x0, float y0, float x1, float y1, float width, uint16_t c);
    // Soft radial light, alpha falls off quadratically; ordered-dithered to hide 565 banding.
    void glow(float cx, float cy, float r, uint16_t c, uint8_t strength);
    // Vertical gradient fill with dither (for subtle backgrounds only).
    void vgradient(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t top, uint16_t bottom);

    // --- text (UTF-8). y is the baseline. Returns advance width.
    int16_t text(int16_t x, int16_t y, const char* s, const Font& f, uint16_t c,
                 Align a = Align::Left, int8_t tracking = 0);
    // Text with a vertical colour gradient over the font ascent (hero clock).
    int16_t textGradient(int16_t x, int16_t y, const char* s, const Font& f, uint16_t top, uint16_t bottom,
                         Align a = Align::Left, int8_t tracking = 0);
    static int16_t textWidth(const char* s, const Font& f, int8_t tracking = 0);
    // Truncate `s` with an ellipsis into `out` so it fits maxW.
    static void ellipsize(const char* s, const Font& f, int16_t maxW, char* out, size_t outLen);

    // --- images
    void image(int16_t x, int16_t y, int16_t w, int16_t h, RowSource src, void* ctx, float radius = 0);

    // Blend one pixel; `blend` applies the scene offset, `blendRaw` takes screen coords.
    inline void blend(int16_t x, int16_t y, uint16_t c, uint8_t a) { blendRaw(x + dx_, y + dy_, c, a); }
    inline void blendRaw(int16_t x, int16_t y, uint16_t c, uint8_t a);

private:
    bool rowSpan(int16_t y, int16_t& x0, int16_t& x1) const;  // clip a row range
    bool clipRect(int16_t& x0, int16_t& y0, int16_t& x1, int16_t& y1) const;
    void hspan(int16_t y, int16_t x0, int16_t x1, uint16_t c);  // exclusive x1, opaque
    void covPixel(int16_t x, int16_t y, uint16_t c, float cov);
    void arcCore(float cx, float cy, float r, float thick, float a0, float a1, uint16_t c0, uint16_t c1, bool grad,
                 bool caps = true);
    bool gradOn_ = false;
    uint16_t gradTop_ = 0, gradBot_ = 0;
    int16_t gradY0_ = 0, gradH_ = 1;

    uint16_t* buf_ = nullptr;
    Rect win_;
    Rect clip_;
    bool hasClip_ = false;
    int16_t dx_ = 0, dy_ = 0;
    uint8_t opacity_ = 255;
};

// Decode one UTF-8 code point, advancing p.
uint16_t utf8Next(const char*& p);
bool fontGlyph(const Font& f, uint16_t cp, Glyph& out);

inline void Canvas::blendRaw(int16_t x, int16_t y, uint16_t c, uint8_t a) {
    if (x < win_.x || y < win_.y || x >= win_.r() || y >= win_.b()) return;
    if (hasClip_ && (x < clip_.x || y < clip_.y || x >= clip_.r() || y >= clip_.b())) return;
    if (opacity_ != 255) a = uint8_t((uint16_t(a) * opacity_) >> 8);
    uint16_t& p = buf_[(y - win_.y) * win_.w + (x - win_.x)];
    p = blend565(c, p, a);
}
