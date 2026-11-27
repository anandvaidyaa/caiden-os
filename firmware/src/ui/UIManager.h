#pragma once
// Owns the current screen, the theme and the frame loop.
// - Partial updates: only dirty rectangles are re-rendered (band renderer), <= 30 fps.
// - Page changes:
//     PushUp / PushDown: hardware-scroll push (ST7789 vertical scroll). The next page is drawn
//       in small slices into the panel's 80 spare RAM rows, ahead of the scroll. Motion starts once
//       80 rows are ready and follows an ease-in-out curve whose peak speed stays below the
//       drawing speed (~1.1-1.3 rows/ms measured), so the scroll never waits for drawing and
//       steps every few ms. A slight backlight "breath" during the slide adds depth.
//     Fade: backlight dip (fallback, e.g. theme changes), eased both ways.
// - Transient page indicator after a page change (1.5 s hold, 200 ms fade).
#include "Screen.h"
#include "../graphics/Renderer.h"
#include "../animations/Animation.h"

enum class Transition : uint8_t { None, PushUp, PushDown, Fade };

class UIManager {
public:
    void begin(Renderer* r);
    // `bright` (0..255, -1 = unchanged) is applied when the new page becomes visible.
    void show(Screen* s, Transition t = Transition::PushUp, int8_t pageIndex = -1, uint8_t pageCount = 0,
              int16_t bright = -1);
    void setTheme(ThemeId id);
    void setBrightness(uint8_t level255);
    // Weather/air alert banner over the page (kind 0 = none). Shown 10 s when it changes, 5 s after each page change.
    void setAlert(const char* text, uint8_t kind);
    void invalidateAll() { dirty_.addAll(); }
    void loop(uint32_t now);
    bool transitioning() const { return phase_ != Phase::Idle; }
    // something was redrawn in the last 150 ms (or a page change runs): the miner then hashes in short slices
    bool animating(uint32_t now) const { return phase_ != Phase::Idle || now - lastDrawAt_ < 150; }
    Screen* current() const { return cur_; }
    Screen* target() const { return phase_ == Phase::Idle ? cur_ : next_; }
    const Theme& themeRef() const { return *theme_; }
    uint32_t lastFrameUs() const { return lastUs_; }
    uint32_t benchFullFrame(uint8_t runs);      // full redraw timing (diagnostics)
    // Breakdown of the last bench: paint / swap / spi / other (microseconds, per frame).
    uint32_t benchPaint = 0, benchSwap = 0, benchSpi = 0;

private:
    enum class Phase : uint8_t { Idle, Push, FadeOut, FadeIn };
    void renderDirty();
    void paint(Canvas& c, Screen* s);
    void stepPush(uint32_t now);
    void drawBanner(Canvas& c);
    void showBanner(uint32_t now, uint32_t hold);
    Renderer* r_ = nullptr;
    Screen* cur_ = nullptr;
    Screen* next_ = nullptr;
    const Theme* theme_ = nullptr;
    DirtySet dirty_;
    Phase phase_ = Phase::Idle;
    Transition trans_ = Transition::None;
    uint32_t phaseStart_ = 0, lastFrame_ = 0, lastUs_ = 0;
    uint16_t pushS0_ = 0, pushK_ = 0, pushS_ = 0;   // scroll origin, rows drawn, rows scrolled
    uint32_t motionStart_ = 0;                       // 0 = still drawing the head start
    uint8_t bright_ = 180;
    int16_t pendingBright_ = -1;
    bool curHeader_ = false;
    uint8_t musicShown_ = 0;                          // header badges shown: 1 music, 2 mining on, 4 mining active
    uint32_t lastEq_ = 0, lastDrawAt_ = 0;
    // page indicator
    int8_t pageIdx_ = -1;
    uint8_t pageCount_ = 0;
    uint32_t indicatorAt_ = 0;
    uint8_t indicatorAlpha_ = 0;
    char alertText_[48] = "";
    uint8_t alertKind_ = 0, bannerAlpha_ = 0;
    uint32_t bannerAt_ = 0, bannerHold_ = 0;
};

extern UIManager g_ui;
