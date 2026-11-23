#pragma once
// Dashboards fed by the companion bridge (design2/render_ecosystem.py: scr_spotify, scr_markets,
// scr_news, scr_calendar). Each is only part of the rotation while it has data.
#include "../ui/Screen.h"

class SpotifyScreen : public Screen {
public:
    const char* name() const override { return "spotify"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
    uint32_t lastSec_ = 0, lastEq_ = 0;
    float eqPhase_ = 0;
};

class MarketsScreen : public Screen {
public:
    const char* name() const override { return "markets"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
    int lastMin_ = -1;
};

class NewsScreen : public Screen {
public:
    const char* name() const override { return "news"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint8_t idx_ = 0;
    uint32_t storyAt_ = 0, lastTick_ = 0;
    int16_t timerY_ = 150;
    bool marquee_ = false;
    uint32_t lastScroll_ = 0;
    uint16_t ver_ = 0;
    int lastMin_ = -1;
};

class CalendarScreen : public Screen {
public:
    const char* name() const override { return "calendar"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
    int lastMin_ = -1;
};

// Quote of the day, internet speed test and home network: data from the companion bridge.
class QuoteScreen : public Screen {
public:
    const char* name() const override { return "quote"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
    int lastMin_ = -1;
};


class LanScreen : public Screen {
public:
    const char* name() const override { return "lan"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
    int lastMin_ = -1;
};
