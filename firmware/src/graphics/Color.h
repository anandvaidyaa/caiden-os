#pragma once
#include <stdint.h>

// RGB565 helpers. All UI colours are authored as 0xRRGGBB and converted at compile time.
constexpr uint16_t rgb(uint32_t hex) {
    return uint16_t(((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F));
}

// Fast 565 alpha blend (alpha 0..255). Classic packed-RGB trick: one multiply.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha) {
    if (alpha >= 250) return fg;
    if (alpha < 6) return bg;
    uint32_t a = (uint32_t(alpha) + 4) >> 3;
    uint32_t b = (bg | (uint32_t(bg) << 16)) & 0x07E0F81Fu;
    uint32_t f = (fg | (uint32_t(fg) << 16)) & 0x07E0F81Fu;
    uint32_t r = ((((f - b) * a) >> 5) + b) & 0x07E0F81Fu;
    return uint16_t((r >> 16) | r);
}

// Lerp between two 565 colours (t 0..255) - used for theme cross-fades and tints.
static inline uint16_t mix565(uint16_t a, uint16_t b, uint8_t t) { return blend565(b, a, t); }

static inline uint8_t r8(uint16_t c) { uint8_t v = (c >> 11) & 31; return (v << 3) | (v >> 2); }
static inline uint8_t g8(uint16_t c) { uint8_t v = (c >> 5) & 63;  return (v << 2) | (v >> 4); }
static inline uint8_t b8(uint16_t c) { uint8_t v = c & 31;         return (v << 3) | (v >> 2); }
static inline uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
