#pragma once
// CAIDEN animation primitives: easing curves + millis()-driven tweens. Never blocking:
// screens sample a tween's value on each update() and mark the affected region dirty.
#include <Arduino.h>
#include <math.h>

namespace anim {

inline float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
inline float linear(float t) { return clamp01(t); }
inline float easeOutCubic(float t) { t = 1 - clamp01(t); return 1 - t * t * t; }
inline float easeInOutCubic(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}
inline float easeInOutSine(float t) { return -(cosf(3.14159265f * clamp01(t)) - 1) / 2; }

typedef float (*Ease)(float);

// A float animating from -> to over `dur` ms.
struct Tween {
    float from = 0, to = 0;
    uint32_t start = 0, dur = 1;
    Ease ease = easeInOutCubic;
    bool running = false;

    void set(float v) { from = to = v; running = false; }
    // `now` must be the same clock the caller later passes to active()/value().
    void animateTo(float target, uint32_t ms, uint32_t now, Ease e = easeInOutCubic) {
        from = value(now);
        to = target; start = now; dur = ms ? ms : 1; ease = e; running = true;
    }
    float progress(uint32_t now) const {
        if (!running) return 1.f;
        int32_t el = int32_t(now - start);
        return el <= 0 ? 0.f : clamp01(float(el) / dur);
    }
    float value(uint32_t now) const {
        if (!running) return to;
        float p = progress(now);
        return from + (to - from) * ease(p);
    }
    // True while animating; clears itself once finished (callers redraw one last time).
    bool active(uint32_t now) {
        if (!running) return false;
        if (int32_t(now - start) >= int32_t(dur)) { running = false; return true; }   // final frame
        return true;
    }
};

}  // namespace anim
