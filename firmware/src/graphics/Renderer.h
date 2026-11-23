#pragma once
// Band renderer: paints any screen region through one small static buffer.
//   renderer.draw(rect, [&](Canvas& c){ ...scene... });
// The scene callback is invoked once per band; primitives clip themselves.
#include "Canvas.h"
#include "../display/St7789.h"

class Renderer {
public:
    static constexpr int16_t BAND_PIXELS = 240 * 24;     // 11.5 KB static, never heap

    explicit Renderer(St7789& d) : disp_(d) {}

    template <typename Paint>
    uint32_t draw(const Rect& region, Paint&& paint) {
        Rect r = region.intersect(Rect(0, 0, 240, 240));
        if (r.empty()) return 0;
        uint32_t t0 = micros();
        int16_t bandH = BAND_PIXELS / r.w;
        if (bandH > r.h) bandH = r.h;
        for (int16_t y = r.y; y < r.b();) {
            int16_t h = (y + bandH > r.b()) ? r.b() - y : bandH;
            int16_t room = int16_t(disp_.rowsUntilWrap(uint16_t(y)));   // never cross the RAM wrap
            if (h > room) h = room;
            Rect win(r.x, y, r.w, h);
            canvas_.begin(buf_, win);
            canvas_.setOffset(0, 0);
            canvas_.setOpacity(255);
            canvas_.clearClip();
            uint32_t tp = micros();
            paint(canvas_);
            paintUs += micros() - tp;
            disp_.setWindow(win.x, win.y, win.w, win.h);
            disp_.pushPixelsSwap(buf_, uint32_t(win.w) * win.h);
            yield();  // keep Wi-Fi + watchdog happy between bands
            y += h;
        }
        return micros() - t0;
    }

    St7789& display() { return disp_; }
    uint32_t paintUs = 0;   // diagnostics: time spent in scene painting (incl. band clear)

private:
    St7789& disp_;
    Canvas canvas_;
    uint16_t buf_[BAND_PIXELS];
};
