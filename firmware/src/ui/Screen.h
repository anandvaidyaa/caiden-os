#pragma once
#include "../graphics/Canvas.h"
#include "Theme.h"

// Up to N dirty rectangles per frame; overlapping/adjacent ones are merged.
class DirtySet {
public:
    static constexpr uint8_t N = 6;
    void add(const Rect& r);
    void addAll() { n_ = 0; add(Rect(0, 0, 240, 240)); }
    void clear() { n_ = 0; }
    uint8_t count() const { return n_; }
    const Rect& operator[](uint8_t i) const { return r_[i]; }
private:
    Rect r_[N];
    uint8_t n_ = 0;
};

// A full-screen page. render() draws the whole scene (it is called per band and
// primitives clip themselves); update() decides what changed and marks it dirty.
class Screen {
public:
    virtual ~Screen() {}
    virtual const char* name() const = 0;
    virtual void enter(uint32_t now) { (void)now; }
    virtual void render(Canvas& c, const Theme& t) = 0;
    virtual void update(uint32_t now, DirtySet& dirty) = 0;
    // Screens can replace the theme's ambient background (Spotify, idle).
    virtual void background(Canvas& c, const Theme& t) { paintBackground(c, t); }
};
