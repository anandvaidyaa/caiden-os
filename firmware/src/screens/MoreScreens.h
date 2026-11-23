#pragma once
// CAIDEN OS 2.0: Productivity, World clock, Cricket, System (design2/render_ecosystem.py
// scr_productivity, scr_world, scr_sports, scr_system).
#include "../ui/Screen.h"

class ProductivityScreen : public Screen {
public:
    const char* name() const override { return "focus"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
    uint32_t lastSec_ = 0;
    int lastMin_ = -1;
};

class WorldScreen : public Screen {
public:
    const char* name() const override { return "world"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
    static constexpr int CITIES = 4;
private:
    void compute();
    int lastMin_ = -1;
    // computed once per minute (tzset is not free; render() runs once per band)
    int16_t mins_[CITIES] = {0}, diff_[CITIES] = {0};
    int16_t localMin_ = 0, utcDiff_ = 0;
    char abbr_[8] = "";
    char weekday_[12] = "";
};

class CricketScreen : public Screen {
public:
    const char* name() const override { return "cricket"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
};

class SystemScreen : public Screen {
public:
    const char* name() const override { return "system"; }
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint32_t last_ = 0;
};

// Sun & Moon: daylight arc from today's sunrise/sunset (weather service) and the moon phase
// (computed on the clock).
class SunScreen : public Screen {
public:
    const char* name() const override { return "sun"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t wver_ = 0;
    int lastMin_ = -1;
};
