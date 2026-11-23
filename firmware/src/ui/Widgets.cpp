#include "Widgets.h"
#include <math.h>
#include <string.h>
#include <Arduino.h>

namespace ui {

int16_t label(Canvas& c, int16_t x, int16_t y, const char* s, uint16_t col, Align a) {
    return c.text(x, y, s, FONT_LABEL, col, a, LABEL_TRACK);
}

void card(Canvas& c, const Theme& t, float x, float y, float w, float h, float r) {
    c.fillRoundRect(x, y, w, h, r, t.surface);
}

void bar(Canvas& c, const Theme& t, float x, float yc, float w, float frac, float h, uint16_t fill) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    c.fillRoundRect(x, yc - h / 2, w, h, h / 2, t.stroke);
    float fw = w * frac;
    if (fw < h) fw = frac > 0 ? h : 0;
    if (fw > 0) c.fillRoundRect(x, yc - h / 2, fw, h, h / 2, fill);
}

void ringGauge(Canvas& c, const Theme& t, float cx, float cy, float r, float thick, float frac, uint16_t col) {
    c.arc(cx, cy, r, thick, 135, 405, t.surfaceHi);
    if (frac > 0.005f) c.arc(cx, cy, r, thick, 135, 135 + 270.f * (frac > 1 ? 1 : frac), col);
}

// ---------------------------------------------------------------- weather icons
static void sun(Canvas& c, const Theme& t, float x, float y, float r, float k, float phase) {
    c.fillCircle(x, y, r, t.sun);
    float rot = phase * 45.f;
    for (int i = 0; i < 8; i++) {
        float a = (i * 45.f + rot) * 0.0174533f, ca = cosf(a), sa = sinf(a);
        c.line(x + ca * (r + 4 * k), y + sa * (r + 4 * k), x + ca * (r + 8 * k), y + sa * (r + 8 * k), 2.2f * k, t.sun);
    }
}
static void moon(Canvas& c, const Theme& t, float x, float y, float r, uint16_t bgc) {
    c.fillCircle(x, y, r, t.sun);
    c.fillCircle(x + r * 0.55f, y - r * 0.35f, r * 0.85f, bgc);   // crescent bite
}
static void cloud(Canvas& c, float x, float y, float k, uint16_t col) {
    c.fillCircle(x - 9 * k, y + 2 * k, 8 * k, col);
    c.fillCircle(x + 1 * k, y - 4 * k, 11 * k, col);
    c.fillCircle(x + 11 * k, y + 3 * k, 7.5f * k, col);
    c.fillRoundRect(x - 17 * k, y + 2 * k, 35 * k, 9 * k, 4.5f * k, col);
}

void weatherIcon(Canvas& c, const Theme& t, Sky kind, float cx, float cy, float size, float phase, bool night) {
    const float k = size / 48.f;
    // cloud drifts +/-1 px over the loop (ease-in-out sine)
    const float drift = sinf((millis() % 8000) / 8000.f * 6.2832f) * k;   // 8 s period (spec)
    // Background behind the icon, used to cut the moon crescent
    const uint16_t bgc = t.surface;
    switch (kind) {
        case Sky::Clear:
            if (night) moon(c, t, cx, cy, 11 * k, bgc);
            else sun(c, t, cx, cy, 10 * k, k, phase);
            break;
        case Sky::Partly:
            if (night) moon(c, t, cx - 7 * k, cy - 7 * k, 8 * k, bgc);
            else sun(c, t, cx - 7 * k, cy - 7 * k, 8 * k, k, phase);
            cloud(c, cx + 4 * k + drift, cy + 6 * k, k, t.cloud);
            break;
        case Sky::Fog:
            cloud(c, cx + drift, cy - 4 * k, k, t.cloud);
            c.line(cx - 15 * k, cy + 13 * k, cx + 13 * k, cy + 13 * k, 2.2f * k, t.text2);
            c.line(cx - 10 * k, cy + 18 * k, cx + 17 * k, cy + 18 * k, 2.2f * k, t.text2);
            break;
        case Sky::Drizzle:
        case Sky::Rain:
        case Sky::Storm: {
            cloud(c, cx + drift, cy - 6 * k, k, t.cloud);
            if (kind == Sky::Storm) {
                c.line(cx + 2 * k, cy + 6 * k, cx - 3 * k, cy + 13 * k, 2.4f * k, t.warn);
                c.line(cx - 3 * k, cy + 13 * k, cx + 3 * k, cy + 13 * k, 2.4f * k, t.warn);
                c.line(cx + 3 * k, cy + 13 * k, cx - 2 * k, cy + 20 * k, 2.4f * k, t.warn);
                break;
            }
            int drops = kind == Sky::Drizzle ? 2 : 3;
            static const float DX3[3] = {-9, 0, 9}, DX2[2] = {-5, 5};
            for (int i = 0; i < drops; i++) {
                float dx = (drops == 3 ? DX3 : DX2)[i];
                float o = fmodf(phase * 7.5f + i * 0.4f, 1.f) * 10 * k;   // 0.8 s fall per 6 s loop
                c.line(cx + dx * k, cy + 9 * k + o, cx + dx * k - 2 * k, cy + 14 * k + o, 2.2f * k, t.accent);
            }
            break;
        }
        case Sky::Snow:
            cloud(c, cx + drift, cy - 6 * k, k, t.cloud);
            for (int i = 0; i < 3; i++) {
                float o = fmodf(phase * 3.f + i * 0.33f, 1.f) * 8 * k;
                c.fillCircle(cx + (i - 1) * 9 * k, cy + 10 * k + o, 1.8f * k, t.text);
            }
            break;
        case Sky::Cloudy:
        case Sky::Unknown:
        default:
            cloud(c, cx + drift, cy, k, kind == Sky::Unknown ? t.stroke : t.cloud);
            break;
    }
}

Sky skyFromWmo(int code) {
    if (code < 0) return Sky::Unknown;
    if (code <= 1) return Sky::Clear;
    if (code == 2) return Sky::Partly;
    if (code == 3) return Sky::Cloudy;
    if (code == 45 || code == 48) return Sky::Fog;
    if (code >= 51 && code <= 57) return Sky::Drizzle;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return Sky::Rain;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
    if (code >= 95) return Sky::Storm;
    return Sky::Cloudy;
}

const char* skyName(int code) {
    if (code < 0) return "Fetching weather";
    if (code == 0) return "Clear";
    if (code == 1) return "Mostly clear";
    if (code == 2) return "Partly cloudy";
    if (code == 3) return "Overcast";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 65) return "Rain";
    if (code == 66 || code == 67) return "Freezing rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code == 85 || code == 86) return "Snow showers";
    if (code >= 95) return "Thunderstorm";
    return "Cloudy";
}


void statusBar(Canvas& c, const Theme& t, const char* title, const char* timeStr, bool offline) {
    label(c, M, 22, title, t.text2);
    int16_t tw = c.text(W - M, 22, timeStr, FONT_MONO, t.text, Align::Right);
    if (offline) c.fillCircle(W - M - tw - 8, 18, 2.5f, t.accent);
}

bool marquee(Canvas& c, int16_t x, int16_t y, int16_t w, const char* s, const Font& f, uint16_t col, uint32_t el) {
    int16_t tw = Canvas::textWidth(s, f);
    if (tw <= w) { c.text(x, y, s, f, col); return false; }
    const int16_t gap = 24, cycle = tw + gap;
    const uint32_t hold = 2000, scrollMs = uint32_t(cycle) * 1000 / 30;
    uint32_t ph = el % (hold + scrollMs);
    int16_t off = ph < hold ? 0 : int16_t((ph - hold) * 30 / 1000);
    const bool had = c.hasClip();
    const Rect prev = c.clip();
    Rect mine(x, int16_t(y - f.asc - 2), w, int16_t(f.asc + f.desc + 4));
    c.setClip(had ? mine.intersect(prev) : mine);
    c.text(x - off, y, s, f, col);
    c.text(x - off + cycle, y, s, f, col);
    if (had) c.setClip(prev); else c.clearClip();
    return true;
}

void pageDots(Canvas& c, const Theme& t, uint8_t index, uint8_t count) {
    float x0 = 120 - (count - 1) * 7 / 2.f;
    for (uint8_t i = 0; i < count; i++) {
        if (i == index) c.fillRoundRect(x0 + i * 7 - 5, 231, 10, 3, 1.5f, t.text);
        else c.fillCircle(x0 + i * 7, 232.5f, 1.3f, t.stroke);
    }
}

}  // namespace ui
