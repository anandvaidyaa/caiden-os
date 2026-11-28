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

// Lyrics, "Kinetic Poster": the sung line set like a small animated music poster (see BridgeScreens.cpp).
static const uint8_t KT_MAX_TOK = 20;
struct KtTok { int16_t x, y; uint8_t off, font; bool big, ell; };   // font: 0 hero, 1 display, 2 body
struct KtLayout { char buf[64]; KtTok t[KT_MAX_TOK]; uint8_t n = 0; int16_t h = 0; };
class LyricsScreen : public Screen {
public:
    const char* name() const override { return "lyrics"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    void relayout(int i, uint32_t now, bool animate);
    Rect wordRect(int j) const;
    KtLayout cur_, old_;                  // poster layout of the sung line, and of the one lifting away
    uint16_t ver_ = 0;
    uint32_t lastSec_ = 0, lastRule_ = 0, lastAnim_ = 0, lastNext_ = 0;
    uint32_t now_ = 0, pos_ = 0;          // frame time and song position, fixed for all bands of a frame
    uint32_t lineAt_ = 0xFFFFFFFF, lineSet_ = 0, activeAt_ = 0;
    int active_ = -2;                     // word being sung (WORD mode), -1 none
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
