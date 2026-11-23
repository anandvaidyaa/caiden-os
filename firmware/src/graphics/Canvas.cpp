#include "Canvas.h"
#include <math.h>
#include <string.h>

// 4x4 Bayer matrix (0..15) for ordered dithering into RGB565.
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Unbiased ordered dither of an 8-bit RGB value into RGB565 (DESIGN_SYSTEM.md 1):
//   q = floor(v * (2^n - 1) / 255 + (B + 0.5) / 16)
static inline uint16_t dither565(int r, int g, int b, int B) {
    // fixed point: v*31/255 = v*7967/65536, v*63/255 = v*16191/65536, (B+.5)/16 = (2B+1)*2048/65536
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    const uint32_t t = uint32_t(2 * B + 1) << 11;
    return uint16_t((((uint32_t(r) * 7967u + t) >> 16) << 11) | (((uint32_t(g) * 16191u + t) >> 16) << 5) |
                    ((uint32_t(b) * 7967u + t) >> 16));
}

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline int16_t fl(float v) { return int16_t(floorf(v)); }
static inline int16_t cl(float v) { return int16_t(ceilf(v)); }

Rect Rect::unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    int16_t nx = x < o.x ? x : o.x, ny = y < o.y ? y : o.y;
    int16_t nr = r() > o.r() ? r() : o.r(), nb = b() > o.b() ? b() : o.b();
    return Rect(nx, ny, nr - nx, nb - ny);
}
Rect Rect::intersect(const Rect& o) const {
    int16_t nx = x > o.x ? x : o.x, ny = y > o.y ? y : o.y;
    int16_t nr = r() < o.r() ? r() : o.r(), nb = b() < o.b() ? b() : o.b();
    if (nr <= nx || nb <= ny) return Rect();
    return Rect(nx, ny, nr - nx, nb - ny);
}

// ---------------------------------------------------------------- clipping
bool Canvas::clipRect(int16_t& x0, int16_t& y0, int16_t& x1, int16_t& y1) const {
    Rect c = hasClip_ ? win_.intersect(clip_) : win_;
    if (x0 < c.x) x0 = c.x;
    if (y0 < c.y) y0 = c.y;
    if (x1 > c.r()) x1 = c.r();
    if (y1 > c.b()) y1 = c.b();
    return x0 < x1 && y0 < y1;
}

void Canvas::hspan(int16_t y, int16_t x0, int16_t x1, uint16_t c) {
    int16_t y1 = y + 1;
    if (!clipRect(x0, y, x1, y1)) return;
    uint16_t* p = buf_ + (y - win_.y) * win_.w + (x0 - win_.x);
    if (opacity_ == 255) {
        for (int16_t x = x0; x < x1; x++) *p++ = c;
    } else {
        for (int16_t x = x0; x < x1; x++, p++) *p = blend565(c, *p, opacity_);
    }
}

void Canvas::covPixel(int16_t x, int16_t y, uint16_t c, float cov) {
    if (cov <= 0.004f) return;
    blendRaw(x, y, c, cov >= 1 ? 255 : uint8_t(cov * 255.f + 0.5f));
}

// ---------------------------------------------------------------- fills
void Canvas::fill(uint16_t c) {
    for (int32_t i = 0, n = int32_t(win_.w) * win_.h; i < n; i++) buf_[i] = c;
}

void Canvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    x += dx_; y += dy_;
    int16_t x1 = x + w, y1 = y + h;
    if (!clipRect(x, y, x1, y1)) return;
    for (int16_t yy = y; yy < y1; yy++) hspan(yy, x, x1, c);
}

// Edge coverage without sqrt: cov = 0.5 + (r^2 - d^2) / (2r)  (accurate within ~1 px of the edge).
// Fixed point: positions in 1/16 px, squared distances in 1/256 px^2, coverage 0..255.
static inline int32_t covLinear(int32_t diff, int32_t k, int32_t lim) {
    if (diff >= lim) return 255;
    if (diff <= -lim) return 0;
    int32_t c = 128 + ((diff * k) >> 16);
    return c < 0 ? 0 : (c > 255 ? 255 : c);
}

void Canvas::fillCircle(float cx, float cy, float r, uint16_t c) {
    cx += dx_; cy += dy_;
    if (r <= 0) return;
    int16_t x0 = fl(cx - r - 1), x1 = cl(cx + r + 1), y0 = fl(cy - r - 1), y1 = cl(cy + r + 1);
    if (!clipRect(x0, y0, x1, y1)) return;
    const int32_t CX = int32_t(cx * 16), CY = int32_t(cy * 16);
    const int32_t r2 = int32_t(r * r * 256), k = int32_t(255.f * 65536.f / (512.f * r)), lim = int32_t(r * 512.f) + 512;
    const float ro = r + 0.5f, ri = r - 0.5f;
    for (int16_t y = y0; y < y1; y++) {
        float fyf = y + 0.5f - cy, fy2f = fyf * fyf;
        if (fy2f > ro * ro) continue;
        float outer = sqrtf(ro * ro - fy2f);                       // one sqrt per row
        int16_t xa = fl(cx - outer), xb = cl(cx + outer);
        if (xa < x0) xa = x0;
        if (xb > x1 - 1) xb = x1 - 1;
        int16_t ia = 1, ib = 0;
        if (ri > 0 && fy2f < ri * ri) {
            float inner = sqrtf(ri * ri - fy2f);
            ia = cl(cx - inner - 0.5f); ib = fl(cx + inner - 0.5f);
        }
        const int32_t fy = (int32_t(y) * 16 + 8) - CY, fy2 = fy * fy;
        for (int16_t x = xa; x <= xb; x++) {
            if (x >= ia && x <= ib) { x = ib; continue; }
            int32_t fx = (int32_t(x) * 16 + 8) - CX;
            int32_t cov = covLinear(r2 - (fx * fx + fy2), k, lim);
            if (cov) blendRaw(x, y, c, uint8_t(cov));
        }
        if (ib >= ia) hspan(y, ia, ib + 1, c);
    }
}

void Canvas::ring(float cx, float cy, float r, float thick, uint16_t c) {
    arcCore(cx, cy, r, thick, 0, 360, c, c, false);
}

void Canvas::arc(float cx, float cy, float r, float thick, float a0, float a1, uint16_t c, bool caps) {
    arcCore(cx, cy, r, thick, a0, a1, c, c, false, caps);
}

void Canvas::arcGradient(float cx, float cy, float r, float thick, float a0, float a1, uint16_t c0, uint16_t c1,
                         bool caps) {
    arcCore(cx, cy, r, thick, a0, a1, c0, c1, true, caps);
}

void Canvas::fillGrid(const uint8_t* g) {
    const int N = GRID_N;
    int32_t row[GRID_N * 3];
    for (int16_t y = win_.y; y < win_.b(); y++) {
        int gy = y >> 3, fy = y & 7, gy1 = gy + 1 < N ? gy + 1 : N - 1;
        const uint8_t* a = g + gy * N * 3;
        const uint8_t* b = g + gy1 * N * 3;
        for (int j = 0; j < N * 3; j++) row[j] = int32_t(a[j]) * (8 - fy) + int32_t(b[j]) * fy;   // value * 8
        uint16_t* p = buf_ + (y - win_.y) * win_.w;
        const uint8_t* B = BAYER[y & 3];
        for (int16_t x = win_.x; x < win_.r(); x++) {
            int gx = x >> 3, fx = x & 7, gx1 = gx + 1 < N ? gx + 1 : N - 1;
            const int32_t* c0 = &row[gx * 3];
            const int32_t* c1 = &row[gx1 * 3];
            int r = (c0[0] * (8 - fx) + c1[0] * fx) >> 6;                                     // value * 64 / 64
            int gg = (c0[1] * (8 - fx) + c1[1] * fx) >> 6;
            int bb = (c0[2] * (8 - fx) + c1[2] * fx) >> 6;
            *p++ = dither565(r, gg, bb, B[x & 3]);
        }
    }
}

// Fast atan2 in degrees, [0, 360), max error ~0.3 deg (only used for gradient colour position).
static float fastAtan2Deg(float y, float x) {
    float ax = fabsf(x), ay = fabsf(y);
    float mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax;
    if (mx < 1e-6f) return 0;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    if (y < 0) r = -r;
    float d = r * 57.2957795f;
    return d < 0 ? d + 360.f : d;
}

// Arc band between radius r-thick and r, from a0 to a1 degrees (0 = 3 o'clock, clockwise),
// round caps. No per-pixel sqrt/atan2: radial coverage is linear in d^2, the sector test uses
// cross products against the start/end vectors, and only the sector's bounding box is visited.
void Canvas::arcCore(float cx, float cy, float r, float thick, float a0, float a1, uint16_t c0, uint16_t c1, bool grad,
                     bool caps) {
    float span = a1 - a0;
    const bool full = span >= 359.9f;
    if (span <= 0) return;
    if (span > 360) span = 360;
    cx += dx_; cy += dy_;
    {   // cheap reject before any trig: is the whole circle outside the window/clip?
        int16_t qx0 = fl(cx - r - 1), qx1 = cl(cx + r + 1), qy0 = fl(cy - r - 1), qy1 = cl(cy + r + 1);
        if (!clipRect(qx0, qy0, qx1, qy1)) return;
    }
    const float ri = r - thick;
    // --- bounding box of the sector (endpoints at both radii + axis extremes inside the span)
    float bx0 = cx - r, bx1 = cx + r, by0 = cy - r, by1 = cy + r;
    const float rad0 = a0 * 0.01745329f, rad1 = a1 * 0.01745329f;
    const float s0x = cosf(rad0), s0y = sinf(rad0), e0x = cosf(rad1), e0y = sinf(rad1);
    if (!full) {
        float rr = ri > 0 ? ri : 0;
        bx0 = bx1 = cx + s0x * r; by0 = by1 = cy + s0y * r;
        auto inc = [&](float px, float py) {
            if (px < bx0) bx0 = px;
            if (px > bx1) bx1 = px;
            if (py < by0) by0 = py;
            if (py > by1) by1 = py;
        };
        inc(cx + e0x * r, cy + e0y * r);
        inc(cx + s0x * rr, cy + s0y * rr);
        inc(cx + e0x * rr, cy + e0y * rr);
        static const int8_t AX[4] = {1, 0, -1, 0}, AY[4] = {0, 1, 0, -1};
        for (int q = 0; q < 4; q++) {                   // axis extremes inside the span
            float rel = fmodf(q * 90.f - a0, 360.f); if (rel < 0) rel += 360.f;
            if (rel <= span) inc(cx + r * AX[q], cy + r * AY[q]);
        }
    }
    int16_t x0 = fl(bx0 - 1), x1 = cl(bx1 + 1), y0 = fl(by0 - 1), y1 = cl(by1 + 1);
    if (!clipRect(x0, y0, x1, y1)) return;

    const int32_t CX = int32_t(cx * 16), CY = int32_t(cy * 16);
    const int32_t ro2 = int32_t(r * r * 256), kO = int32_t(255.f * 65536.f / (512.f * r)), limO = int32_t(r * 512.f) + 512;
    const bool hasIn = ri > 0.5f;
    const int32_t ri2 = hasIn ? int32_t(ri * ri * 256) : 0;
    const int32_t kI = hasIn ? int32_t(255.f * 65536.f / (512.f * ri)) : 0, limI = int32_t(ri * 512.f) + 512;
    const int32_t sx = int32_t(s0x * 1024), sy = int32_t(s0y * 1024), ex = int32_t(e0x * 1024), ey = int32_t(e0y * 1024);
    const bool small = span <= 180.f;
    const float roF = r + 0.5f, rhF = ri - 0.5f;
    const int cr0 = r8(c0), cg0 = g8(c0), cb0 = b8(c0), cr1 = r8(c1), cg1 = g8(c1), cb1 = b8(c1);
    const float invSpan256 = 256.f / span;

    for (int16_t y = y0; y < y1; y++) {
        float fyf = y + 0.5f - cy, fy2f = fyf * fyf;
        if (fy2f > roF * roF) continue;
        float outer = sqrtf(roF * roF - fy2f);
        int16_t xa = fl(cx - outer), xb = cl(cx + outer);
        if (xa < x0) xa = x0;
        if (xb > x1 - 1) xb = x1 - 1;
        int16_t ha = 1, hb = 0;                          // pixels fully inside the hole
        if (hasIn && rhF > 0 && fy2f < rhF * rhF) {
            float hole = sqrtf(rhF * rhF - fy2f);
            ha = cl(cx - hole + 0.5f); hb = fl(cx + hole - 0.5f) - 1;
        }
        const int32_t fy = (int32_t(y) * 16 + 8) - CY, fy2 = fy * fy;
        for (int16_t x = xa; x <= xb; x++) {
            if (x >= ha && x <= hb) { x = hb; continue; }
            const int32_t fx = (int32_t(x) * 16 + 8) - CX;
            const int32_t d2 = fx * fx + fy2;
            int32_t cov = covLinear(ro2 - d2, kO, limO);
            if (!cov) continue;
            if (hasIn) { int32_t ci = covLinear(d2 - ri2, kI, limI); if (ci < cov) cov = ci; if (!cov) continue; }
            if (!full) {
                // y grows downward, so "clockwise from v" means cross(v, p) >= 0
                int32_t cs = sx * fy - sy * fx;          // cross(s, p)
                int32_t ce = fx * ey - fy * ex;          // cross(p, e)
                bool in = small ? (cs >= 0 && ce >= 0) : !(cs < 0 && ce < 0);
                if (!in) continue;
            }
            uint16_t col = c0;
            if (grad) {
                float ang = fastAtan2Deg(float(fy), float(fx));
                float rel = ang - a0; while (rel < 0) rel += 360.f; while (rel >= 360.f) rel -= 360.f;
                int t = rel > span ? (rel > 180.f + span * 0.5f ? 0 : 256) : int(rel * invSpan256);
                col = pack565(uint8_t(cr0 + (((cr1 - cr0) * t) >> 8)), uint8_t(cg0 + (((cg1 - cg0) * t) >> 8)),
                              uint8_t(cb0 + (((cb1 - cb0) * t) >> 8)));
            }
            blendRaw(x, y, col, uint8_t(cov));
        }
    }
    if (!full && caps) {   // round caps
        const float rm = r - thick * 0.5f, cr = thick * 0.5f;
        fillCircle(cx - dx_ + rm * s0x, cy - dy_ + rm * s0y, cr, c0);
        fillCircle(cx - dx_ + rm * e0x, cy - dy_ + rm * e0y, cr, grad ? c1 : c0);
    }
}

// Disc (cx,cy,r) minus disc (bx,by,br): crescent moons that stay transparent over gradients.
void Canvas::fillCrescent(float cx, float cy, float r, float bx, float by, float br, uint16_t c) {
    cx += dx_; cy += dy_; bx += dx_; by += dy_;
    int16_t x0 = fl(cx - r - 1), x1 = cl(cx + r + 1), y0 = fl(cy - r - 1), y1 = cl(cy + r + 1);
    if (!clipRect(x0, y0, x1, y1)) return;
    const int32_t CX = int32_t(cx * 16), CY = int32_t(cy * 16), BX = int32_t(bx * 16), BY = int32_t(by * 16);
    const int32_t r2 = int32_t(r * r * 256), k = int32_t(255.f * 65536.f / (512.f * r)), lim = int32_t(r * 512.f) + 512;
    const int32_t b2 = int32_t(br * br * 256), kb = int32_t(255.f * 65536.f / (512.f * br)), limb = int32_t(br * 512.f) + 512;
    for (int16_t y = y0; y < y1; y++) {
        const int32_t fy = (int32_t(y) * 16 + 8) - CY, gy = (int32_t(y) * 16 + 8) - BY;
        for (int16_t x = x0; x < x1; x++) {
            const int32_t fx = (int32_t(x) * 16 + 8) - CX, gx = (int32_t(x) * 16 + 8) - BX;
            int32_t a = covLinear(r2 - (fx * fx + fy * fy), k, lim);
            if (!a) continue;
            int32_t bcov = covLinear(b2 - (gx * gx + gy * gy), kb, limb);
            int32_t cov = a < 255 - bcov ? a : 255 - bcov;
            if (cov) blendRaw(x, y, c, uint8_t(cov));
        }
    }
}

void Canvas::line(float x0f, float y0f, float x1f, float y1f, float width, uint16_t c) {
    x0f += dx_; x1f += dx_; y0f += dy_; y1f += dy_;
    const float hw = width * 0.5f;
    int16_t bx0 = fl((x0f < x1f ? x0f : x1f) - hw - 1), bx1 = cl((x0f > x1f ? x0f : x1f) + hw + 1);
    int16_t by0 = fl((y0f < y1f ? y0f : y1f) - hw - 1), by1 = cl((y0f > y1f ? y0f : y1f) + hw + 1);
    if (!clipRect(bx0, by0, bx1, by1)) return;
    const float vx = x1f - x0f, vy = y1f - y0f, len2 = vx * vx + vy * vy;
    const float inv = len2 > 1e-6f ? 1.f / len2 : 0;
    for (int16_t y = by0; y < by1; y++) {
        float py = y + 0.5f - y0f;
        for (int16_t x = bx0; x < bx1; x++) {
            float px = x + 0.5f - x0f;
            float t = (px * vx + py * vy) * inv;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float ex = px - vx * t, ey = py - vy * t;
            float d2 = ex * ex + ey * ey;
            if (d2 > (hw + 0.5f) * (hw + 0.5f)) continue;
            covPixel(x, y, c, hw - sqrtf(d2) + 0.5f);
        }
    }
}

// Coverage of a rounded rectangle at pixel centre (px, py). Uses the box
// overlap on straight edges and circle distance inside the corner squares.
static float rrCoverage(float px, float py, float x, float y, float w, float h, float r) {
    float cx = -1, cy = -1;
    if (px < x + r) cx = x + r; else if (px > x + w - r) cx = x + w - r;
    if (py < y + r) cy = y + r; else if (py > y + h - r) cy = y + h - r;
    if (cx >= 0 && cy >= 0) {
        float dx = px - cx, dy = py - cy;
        return clamp01(r - sqrtf(dx * dx + dy * dy) + 0.5f);
    }
    float cxv = clamp01(px + 0.5f - x) * clamp01(x + w - (px - 0.5f));
    float cyv = clamp01(py + 0.5f - y) * clamp01(y + h - (py - 0.5f));
    if (cxv > 1) cxv = 1;
    return clamp01(cxv) * clamp01(cyv);
}

void Canvas::fillRoundRect(float x, float y, float w, float h, float r, uint16_t c) {
    x += dx_; y += dy_;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    int16_t x0 = fl(x), x1 = cl(x + w), y0 = fl(y), y1 = cl(y + h);
    if (!clipRect(x0, y0, x1, y1)) return;
    // solid interior bounds (pixels fully inside the straight part)
    const int16_t sx0 = cl(x + r), sx1 = fl(x + w - r), sy0 = cl(y), sy1 = fl(y + h);
    const int16_t ex0 = cl(x), ex1 = fl(x + w);
    for (int16_t yy = y0; yy < y1; yy++) {
        bool rowSolid = yy >= sy0 && yy + 1 <= sy1;
        bool inCornerRows = (yy + 1 > y + r) && (yy < y + h - r);
        if (rowSolid && inCornerRows) {
            // middle rows: AA only at fractional left/right edges
            for (int16_t xx = x0; xx < ex0; xx++) covPixel(xx, yy, c, rrCoverage(xx + .5f, yy + .5f, x, y, w, h, r));
            hspan(yy, ex0, ex1, c);
            for (int16_t xx = ex1; xx < x1; xx++) covPixel(xx, yy, c, rrCoverage(xx + .5f, yy + .5f, x, y, w, h, r));
        } else {
            int16_t a = x0;
            if (rowSolid) {
                for (; a < sx0 && a < x1; a++) covPixel(a, yy, c, rrCoverage(a + .5f, yy + .5f, x, y, w, h, r));
                if (sx1 > a) { hspan(yy, a, sx1, c); a = sx1; }
            }
            for (; a < x1; a++) covPixel(a, yy, c, rrCoverage(a + .5f, yy + .5f, x, y, w, h, r));
        }
    }
}

// Stroke = 4 straight edges (solid spans) + 4 quarter arcs. Never evaluates interior pixels.
void Canvas::strokeRoundRect(float x, float y, float w, float h, float r, float t, uint16_t c) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    // straight edges (integer-aligned for crisp 1 px strokes)
    int16_t ix = int16_t(lroundf(x)), iy = int16_t(lroundf(y)), iw = int16_t(lroundf(w)), ih = int16_t(lroundf(h));
    int16_t ir = int16_t(lroundf(r)), it = int16_t(t < 1 ? 1 : lroundf(t));
    fillRect(ix + ir, iy, iw - 2 * ir, it, c);              // top
    fillRect(ix + ir, iy + ih - it, iw - 2 * ir, it, c);    // bottom
    fillRect(ix, iy + ir, it, ih - 2 * ir, c);              // left
    fillRect(ix + iw - it, iy + ir, it, ih - 2 * ir, c);    // right
    if (ir <= 0) return;
    // corners: quarter arcs (outer radius r), no caps needed but harmless at this size
    arcCore(ix + ir, iy + ir, ir, t, 180, 270, c, c, false, false);
    arcCore(ix + iw - ir, iy + ir, ir, t, 270, 360, c, c, false, false);
    arcCore(ix + iw - ir, iy + ih - ir, ir, t, 0, 90, c, c, false, false);
    arcCore(ix + ir, iy + ih - ir, ir, t, 90, 180, c, c, false, false);
}

void Canvas::glow(float cxf, float cyf, float rf, uint16_t c, uint8_t strength) {
    // Integer-only: falloff a = strength * (1 - d^2/r^2)^2, d^2 updated incrementally per pixel.
    const int32_t cx = int32_t(cxf + dx_), cy = int32_t(cyf + dy_), r = int32_t(rf);
    if (r < 1) return;
    int16_t x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
    if (!clipRect(x0, y0, x1, y1)) return;
    const int32_t r2 = r * r;
    const uint32_t invR2 = (1u << 24) / uint32_t(r2);          // t * invR2 >> 16 maps [0, r2] -> [0, 256]
    const uint32_t str = uint32_t(strength) * opacity_ / 255;
    const int cr = r8(c), cg = g8(c), cb = b8(c);
    for (int16_t y = y0; y < y1; y++) {
        const int32_t fy = y - cy;
        const int32_t fy2 = fy * fy;
        if (fy2 >= r2) continue;
        int32_t fx = x0 - cx;
        int32_t d2 = fx * fx + fy2;
        uint16_t* p = buf_ + (y - win_.y) * win_.w + (x0 - win_.x);
        const uint8_t* B = BAYER[y & 3];
        for (int16_t x = x0; x < x1; x++, p++) {
            if (d2 < r2) {
                uint32_t u = (uint32_t(r2 - d2) * invR2) >> 16;     // 0..256
                uint32_t a = (u * u * str) >> 16;                   // 0..255
                if (a) {
                    int pr = r8(*p), pg = g8(*p), pb = b8(*p);
                    *p = dither565(pr + (((cr - pr) * int(a)) >> 8), pg + (((cg - pg) * int(a)) >> 8),
                                   pb + (((cb - pb) * int(a)) >> 8), B[x & 3]);
                }
            }
            d2 += 2 * fx + 1;   // (fx+1)^2 = fx^2 + 2fx + 1
            fx++;
        }
    }
}

void Canvas::vgradient(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t top, uint16_t bottom) {
    x += dx_; y += dy_;
    int16_t x1 = x + w, y1 = y + h, ys = y;
    if (!clipRect(x, y, x1, y1)) return;
    const int tr = r8(top), tg = g8(top), tb = b8(top);
    const int br = r8(bottom), bg = g8(bottom), bb = b8(bottom);
    for (int16_t yy = y; yy < y1; yy++) {
        int t = h > 1 ? ((yy - ys) * 256) / (h - 1) : 0;
        int r = tr + (((br - tr) * t) >> 8), g = tg + (((bg - tg) * t) >> 8), b = tb + (((bb - tb) * t) >> 8);
        uint16_t* p = buf_ + (yy - win_.y) * win_.w + (x - win_.x);
        for (int16_t xx = x; xx < x1; xx++) {
            *p++ = dither565(r, g, b, BAYER[yy & 3][xx & 3]);
        }
    }
}

// ---------------------------------------------------------------- text
uint16_t utf8Next(const char*& p) {
    uint8_t c = uint8_t(*p++);
    if (c < 0x80) return c;
    auto cont = [&p]() -> int {               // never step past the terminating NUL
        if ((uint8_t(*p) & 0xC0) != 0x80) return -1;
        return uint8_t(*p++) & 0x3F;
    };
    if ((c & 0xE0) == 0xC0) { int a = cont(); return a < 0 ? '?' : uint16_t(((c & 0x1F) << 6) | a); }
    if ((c & 0xF0) == 0xE0) {
        int a = cont(); if (a < 0) return '?';
        int b = cont(); if (b < 0) return '?';
        return uint16_t(((c & 0x0F) << 12) | (a << 6) | b);
    }
    while ((uint8_t(*p) & 0xC0) == 0x80) p++;  // skip 4-byte sequences
    return '?';
}

bool fontGlyph(const Font& f, uint16_t cp, Glyph& out) {
    int lo = 0, hi = f.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t v = pgm_read_word(&f.glyphs[mid].cp);
        if (v == cp) { memcpy_P(&out, &f.glyphs[mid], sizeof(Glyph)); return true; }
        if (v < cp) lo = mid + 1; else hi = mid - 1;
    }
    return false;
}

int16_t Canvas::textWidth(const char* s, const Font& f, int8_t tracking) {
    int16_t w = 0; bool first = true;
    Glyph g;
    while (*s) {
        uint16_t cp = utf8Next(s);
        if (!fontGlyph(f, cp, g) && !fontGlyph(f, '?', g)) continue;
        if (!first) w += tracking;
        w += g.adv; first = false;
    }
    return w;
}

void Canvas::ellipsize(const char* s, const Font& f, int16_t maxW, char* out, size_t outLen) {
    size_t n = strlen(s);
    if (n >= outLen) n = outLen - 1;
    memcpy(out, s, n); out[n] = 0;
    if (textWidth(out, f) <= maxW) return;
    static const char ELL[] = "\xE2\x80\xA6";  // …
    while (n > 0) {
        do { n--; } while (n > 0 && (uint8_t(out[n]) & 0xC0) == 0x80);  // UTF-8 boundary
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n + 4 > outLen) continue;
        memcpy(out + n, ELL, 4);
        if (textWidth(out, f) <= maxW) return;
    }
    out[0] = 0;
}

int16_t Canvas::text(int16_t x, int16_t y, const char* s, const Font& f, uint16_t c, Align a, int8_t tracking) {
    int16_t w = textWidth(s, f, tracking);
    if (a == Align::Center) x -= w / 2;
    else if (a == Align::Right) x -= w;
    x += dx_; y += dy_;
    // quick reject whole line against the window rows
    Rect wr = hasClip_ ? win_.intersect(clip_) : win_;
    if (y - f.asc - 2 >= wr.b() || y + f.desc + 2 < wr.y || x >= wr.r() || x + w < wr.x) return w;
    Glyph g;
    int16_t pen = x;
    bool first = true;
    while (*s) {
        uint16_t cp = utf8Next(s);
        if (!fontGlyph(f, cp, g) && !fontGlyph(f, '?', g)) continue;
        if (!first) pen += tracking;
        first = false;
        int16_t gx = pen + g.x0, gy = y + g.y0;
        pen += g.adv;
        if (!g.w || gx >= wr.r() || gx + g.w <= wr.x || gy >= wr.b() || gy + g.h <= wr.y) continue;
        int16_t ry0 = gy < wr.y ? wr.y - gy : 0, ry1 = gy + g.h > wr.b() ? wr.b() - gy : g.h;
        int16_t rx0 = gx < wr.x ? wr.x - gx : 0, rx1 = gx + g.w > wr.r() ? wr.r() - gx : g.w;
        for (int16_t ry = ry0; ry < ry1; ry++) {
            uint32_t base = uint32_t(ry) * g.w;
            uint16_t* p = buf_ + (gy + ry - win_.y) * win_.w + (gx + rx0 - win_.x);
            uint16_t rowCol = c;
            if (gradOn_) {   // vertical gradient across the font's ascent (clock numerals)
                int t = (int(gy + ry) - gradY0_) * 256 / gradH_;
                t = t < 0 ? 0 : (t > 256 ? 256 : t);
                rowCol = pack565(uint8_t(r8(gradTop_) + (((r8(gradBot_) - r8(gradTop_)) * t) >> 8)),
                                 uint8_t(g8(gradTop_) + (((g8(gradBot_) - g8(gradTop_)) * t) >> 8)),
                                 uint8_t(b8(gradTop_) + (((b8(gradBot_) - b8(gradTop_)) * t) >> 8)));
            }
            for (int16_t rx = rx0; rx < rx1; rx++, p++) {
                uint32_t idx = base + rx;
                uint8_t byte = pgm_read_byte(f.bits + g.off + (idx >> 1));
                uint8_t v = (idx & 1) ? (byte & 0x0F) : (byte >> 4);
                if (!v) continue;
                uint8_t al = v * 17;
                if (opacity_ != 255) al = uint8_t((uint16_t(al) * opacity_) >> 8);
                *p = blend565(rowCol, *p, al);
            }
        }
    }
    return w;
}

// ---------------------------------------------------------------- images
int16_t Canvas::textGradient(int16_t x, int16_t y, const char* s, const Font& f, uint16_t top, uint16_t bottom,
                             Align a, int8_t tracking) {
    gradOn_ = true; gradTop_ = top; gradBot_ = bottom;
    // gradient spans the ink of the first glyph (digit cap height for numerals), like the design sim
    Glyph g;
    const char* p0 = s;
    if (*p0 && fontGlyph(f, utf8Next(p0), g) && g.h) { gradY0_ = y + g.y0 + dy_; gradH_ = g.h; }
    else { gradY0_ = y - f.asc + dy_; gradH_ = f.asc ? f.asc : 1; }
    int16_t w = text(x, y, s, f, top, a, tracking);
    gradOn_ = false;
    return w;
}

void Canvas::image(int16_t x, int16_t y, int16_t w, int16_t h, RowSource src, void* ctx, float radius) {
    x += dx_; y += dy_;
    int16_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clipRect(x0, y0, x1, y1)) return;
    uint16_t row[240];
    const int16_t rc = int16_t(ceilf(radius)) + 1;          // corner squares that need coverage maths
    for (int16_t yy = y0; yy < y1; yy++) {
        if (!src(ctx, yy - y, row, x0 - x, x1 - x0)) continue;
        const bool cornerRow = radius > 0 && (yy - y < rc || y + h - 1 - yy < rc);
        uint16_t* dst = buf_ + (yy - win_.y) * win_.w + (x0 - win_.x);
        for (int16_t xx = x0; xx < x1; xx++, dst++) {
            uint16_t px = row[xx - x0];
            // only pixels inside the four rc x rc corner squares can be partially covered
            if (cornerRow && (xx - x < rc || x + w - 1 - xx < rc)) {
                float cov = rrCoverage(xx + .5f, yy + .5f, x, y, w, h, radius);
                if (cov < 0.999f) { covPixel(xx, yy, px, cov); continue; }
            }
            if (opacity_ == 255) *dst = px;
            else blendRaw(xx, yy, px, 255);
        }
    }
}
