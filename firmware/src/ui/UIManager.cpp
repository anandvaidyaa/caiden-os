#include "UIManager.h"
#include "Widgets.h"
#include "Lumen.h"

UIManager g_ui;

static const uint32_t FRAME_MS = 33;                    // partial updates <= ~30 fps
static const uint32_t FADE_OUT_MS = 150, FADE_IN_MS = 260;
// Push: full page draws take 178-212 ms (bench 2026-11-22), i.e. ~1.1 rows/ms at worst. Smootherstep
// over 440 ms peaks at 240 * 1.875 / 440 = 1.02 rows/ms, and the 80-row head start absorbs the rest.
static const uint32_t PUSH_MS = 440;                    // motion time (after the head start)
static const uint16_t PUSH_AHEAD = 80;                  // spare RAM rows: never draw further ahead
static const uint16_t PUSH_SLICE = 16;                  // rows drawn per loop pass (~14 ms)
static const uint8_t  PUSH_BREATH = 26;                 // backlight dip at mid-slide, /255
static const uint32_t IND_HOLD_MS = 1500, IND_FADE_MS = 200;
static const Rect R_INDICATOR(40, 226, 160, 12);
static const Rect R_BANNER(16, 186, 208, 34);

// ---------------------------------------------------------------- DirtySet
void DirtySet::add(const Rect& in) {
    Rect r = in.intersect(Rect(0, 0, 240, 240));
    if (r.empty()) return;
    for (uint8_t i = 0; i < n_; i++) {
        Rect grown(r_[i].x - 2, r_[i].y - 2, r_[i].w + 4, r_[i].h + 4);
        if (grown.intersects(r)) {
            Rect m = r_[i].unite(r);
            r_[i] = r_[--n_];
            add(m);
            return;
        }
    }
    if (n_ < N) { r_[n_++] = r; return; }
    uint8_t best = 0; int32_t bestCost = INT32_MAX;
    for (uint8_t i = 0; i < n_; i++) {
        Rect u = r_[i].unite(r);
        int32_t cost = int32_t(u.w) * u.h - int32_t(r_[i].w) * r_[i].h;
        if (cost < bestCost) { bestCost = cost; best = i; }
    }
    r_[best] = r_[best].unite(r);
}

// ---------------------------------------------------------------- UIManager
void UIManager::begin(Renderer* r) { r_ = r; theme_ = &theme(ThemeId::Midnight); }

void UIManager::setTheme(ThemeId id) {
    const Theme* t = &theme(id);
    if (t == theme_) return;
    theme_ = t;
    dirty_.addAll();
}

void UIManager::setBrightness(uint8_t level) {
    bright_ = level;
    if (phase_ != Phase::FadeOut && phase_ != Phase::FadeIn) r_->display().setBacklight(level);
}

void UIManager::paint(Canvas& c, Screen* s) {
    s->background(c, *theme_);
    lumen::g_headerDrawn = false;
    s->render(c, *theme_);
    if (s == cur_) curHeader_ = lumen::g_headerDrawn;    // page has a header: it can show the music badge
    if (s == cur_ && indicatorAlpha_ && pageIdx_ >= 0 && pageCount_ > 1) {
        c.setOpacity(indicatorAlpha_);
        ui::pageDots(c, *theme_, uint8_t(pageIdx_), pageCount_);
        c.setOpacity(255);
    }
    if (s == cur_ && bannerAlpha_ && alertKind_ && pageIdx_ >= 0) drawBanner(c);
}

static uint16_t alertColor(const Theme& t, uint8_t kind) {
    switch (kind) {
        case 1: case 3: return t.bad;
        case 2: return t.rain;
        case 4: case 5: return t.warn;
        case 7: return t.accent;
        default: return t.accent2;
    }
}

void UIManager::drawBanner(Canvas& c) {
    const Theme& t = *theme_;
    const uint16_t col = alertColor(t, alertKind_);
    const float x = 18, y = 188, w = 204, h = 30;
    c.setOpacity(uint8_t(bannerAlpha_ * (t.dark ? 235 : 225) / 255));
    c.fillRoundRect(x, y, w, h, 15, t.dark ? t.bg2 : t.surface);
    c.setOpacity(uint8_t(bannerAlpha_ * 46 / 255));
    c.fillRoundRect(x, y, w, h, 15, col);
    c.setOpacity(uint8_t(bannerAlpha_ * 200 / 255));
    c.strokeRoundRect(x, y, w, h, 15, 1, col);
    c.setOpacity(bannerAlpha_);
    c.glow(x + 17, y + 15, 9, col, 120);
    c.fillCircle(x + 17, y + 15, 5, col);
    c.fillRoundRect(x + 16.2f, y + 11.5f, 1.6f, 4.2f, 0.8f, t.dark ? t.bg : t.surface);   // "!" mark
    c.fillCircle(x + 17, y + 18, 0.9f, t.dark ? t.bg : t.surface);
    char fit[48];
    Canvas::ellipsize(alertText_, FONT_L_BODY, int16_t(w - 42), fit, sizeof fit);
    c.text(int16_t(x + 30), int16_t(y + 20), fit, FONT_L_BODY, t.text);
    c.setOpacity(255);
}

void UIManager::showBanner(uint32_t now, uint32_t hold) {
    if (!alertKind_) return;
    bannerAt_ = now;
    bannerHold_ = hold;
}

void UIManager::setAlert(const char* text, uint8_t kind) {
    if (!kind || !text) {
        if (alertKind_ && bannerAlpha_) dirty_.add(R_BANNER);
        alertKind_ = 0; alertText_[0] = 0; bannerAlpha_ = 0;
        return;
    }
    const bool same = kind == alertKind_ && !strcmp(text, alertText_);
    snprintf(alertText_, sizeof alertText_, "%s", text);
    alertKind_ = kind;
    if (!same) { showBanner(millis(), 10000); dirty_.add(R_BANNER); }
}

void UIManager::show(Screen* s, Transition t, int8_t pageIndex, uint8_t pageCount, int16_t bright) {
    if (!s || s == target()) return;               // already showing / heading there
    if (phase_ != Phase::Idle) return;             // let the running transition finish
    pageIdx_ = pageIndex;                          // -1 (idle/setup) hides the indicator
    if (pageIndex >= 0) pageCount_ = pageCount;
    indicatorAlpha_ = 0;
    uint32_t now = millis();
    if (!cur_ || t == Transition::None) {
        cur_ = s; next_ = nullptr;
        cur_->enter(now);
        dirty_.addAll();
        indicatorAt_ = now;
        showBanner(now, 5000);
        if (bright >= 0) setBrightness(uint8_t(bright));
        return;
    }
    pendingBright_ = bright;
    next_ = s;
    trans_ = t;
    phaseStart_ = now;
    dirty_.clear();
    if (t == Transition::Fade) { phase_ = Phase::FadeOut; return; }
    // Push: the next page's rows go to the RAM block that will be visible at the end.
    St7789& d = r_->display();
    pushS0_ = d.scroll();
    pushK_ = pushS_ = 0;
    motionStart_ = 0;
    uint16_t base = t == Transition::PushUp ? (pushS0_ + 240) % St7789::RAM_ROWS
                                            : (pushS0_ + St7789::RAM_ROWS - 240) % St7789::RAM_ROWS;
    d.setMapBase(base);
    next_->enter(now);
    phase_ = Phase::Push;
}

static inline float smootherstep(float t) { t = anim::clamp01(t); return t * t * t * (t * (t * 6 - 15) + 10); }

void UIManager::stepPush(uint32_t /*now*/) {
    St7789& d = r_->display();
    // 1. draw one slice of the next page ahead of the scroll (spare RAM rows only)
    const uint16_t limit = pushS_ + PUSH_AHEAD < 240 ? uint16_t(pushS_ + PUSH_AHEAD) : 240;
    if (pushK_ < limit) {
        const int16_t n = int16_t(limit - pushK_ < PUSH_SLICE ? limit - pushK_ : PUSH_SLICE);
        Rect rows = trans_ == Transition::PushUp ? Rect(0, int16_t(pushK_), 240, n)
                                                 : Rect(0, int16_t(240 - pushK_ - n), 240, n);
        lastUs_ = r_->draw(rows, [this](Canvas& c) { paint(c, next_); });
        pushK_ += uint16_t(n);
    }
    const uint32_t now = millis();
    static uint16_t waits = 0;                           // passes where the curve outran drawing
    if (!motionStart_) {
        if (pushK_ < PUSH_AHEAD) return;                 // head start not ready: hold still
        motionStart_ = now;
        waits = 0;
    }
    // 2. move the scroll window along the curve, never past the rows already drawn
    const float p = anim::clamp01(float(now - motionStart_) / PUSH_MS);
    uint16_t s = uint16_t(240.f * smootherstep(p) + 0.5f);
    if (s > pushK_) { s = pushK_; waits++; }
    if (s > pushS_) {
        pushS_ = s;
        d.setScroll(trans_ == Transition::PushUp ? (pushS0_ + s) % St7789::RAM_ROWS
                                                 : (pushS0_ + St7789::RAM_ROWS - s) % St7789::RAM_ROWS);
    }
    // 3. backlight breath: a slight dip at mid-slide, back to full at the end
    const float br = 1.f - PUSH_BREATH / 255.f * sinf(3.14159265f * float(pushS_) / 240.f);
    static uint8_t lastBl = 0;
    const uint8_t bl = uint8_t(float(bright_) * br + 0.5f);
    if (bl != lastBl) { lastBl = bl; d.setBacklight(bl); }
    if (pushS_ >= 240) {
        d.setBacklight(bright_);
        lastBl = 0;
        Serial.printf("[ui] push %lu ms (head start %lu ms, %u waits for drawing)\n", (unsigned long)(now - phaseStart_),
                      (unsigned long)(motionStart_ - phaseStart_), waits);
        cur_ = next_; next_ = nullptr;
        phase_ = Phase::Idle;
        indicatorAt_ = now;
        showBanner(now, 5000);
        lastFrame_ = 0;
        if (pendingBright_ >= 0) { setBrightness(uint8_t(pendingBright_)); pendingBright_ = -1; }
    }
}

void UIManager::renderDirty() {
    if (!cur_ || !dirty_.count()) return;
    uint32_t us = 0;
    for (uint8_t i = 0; i < dirty_.count(); i++)
        us += r_->draw(dirty_[i], [this](Canvas& c) { paint(c, cur_); });
    dirty_.clear();
    lastUs_ = us;
}

static uint8_t easeOut255(uint32_t t, uint32_t d) {
    return uint8_t(255.f * anim::easeOutCubic(float(t) / d));
}

void UIManager::loop(uint32_t now) {
    if (!cur_ && !next_) return;
    switch (phase_) {
        case Phase::Push:
            stepPush(millis());        // runs every loop pass: smooth, cheap per frame
            return;
        case Phase::FadeOut: {
            uint32_t t = millis() - phaseStart_;
            r_->display().setBacklight(uint16_t(bright_) * (255 - easeOut255(t, FADE_OUT_MS)) / 255);
            if (t < FADE_OUT_MS) return;
            cur_ = next_; next_ = nullptr;
            cur_->enter(now);
            if (pendingBright_ >= 0) { bright_ = uint8_t(pendingBright_); pendingBright_ = -1; }   // fade in to the new level
            dirty_.addAll();
            renderDirty();
            phase_ = Phase::FadeIn;
            phaseStart_ = millis();
            indicatorAt_ = phaseStart_;
            showBanner(phaseStart_, 5000);
            return;
        }
        case Phase::FadeIn: {
            uint32_t t = millis() - phaseStart_;
            r_->display().setBacklight(uint16_t(bright_) * uint8_t(255.f * anim::easeInOutSine(float(t) / FADE_IN_MS)) / 255);
            if (t >= FADE_IN_MS) phase_ = Phase::Idle;
            break;
        }
        default: break;
    }
    if (now - lastFrame_ < FRAME_MS) return;
    lastFrame_ = now;

    // page indicator: hold, then fade (only its strip is redrawn)
    if (pageIdx_ >= 0 && pageCount_ > 1) {
        int32_t sage = int32_t(now - indicatorAt_);
        uint32_t age = sage < 0 ? 0 : uint32_t(sage);
        uint8_t a = age < IND_HOLD_MS ? 255
                  : age < IND_HOLD_MS + IND_FADE_MS ? uint8_t(255 - 255 * (age - IND_HOLD_MS) / IND_FADE_MS) : 0;
        if (a != indicatorAlpha_) { indicatorAlpha_ = a; dirty_.add(R_INDICATOR); }
    }
    // alert banner: 200 ms fade in, hold, 300 ms fade out (only its strip is redrawn)
    if (alertKind_) {
        int32_t sage = int32_t(now - bannerAt_);
        uint32_t age = sage < 0 ? 0 : uint32_t(sage);
        uint8_t a = age < 200 ? uint8_t(age * 255 / 200)
                  : age < bannerHold_ ? 255
                  : age < bannerHold_ + 300 ? uint8_t(255 - 255 * (age - bannerHold_) / 300) : 0;
        if (a != bannerAlpha_) { bannerAlpha_ = a; dirty_.add(R_BANNER); }
    }
    // music badge in the page header while Spotify plays: ~12 fps, only its small rect is redrawn
    const uint8_t badges = uint8_t((lumen::musicPlaying() ? 1 : 0) | (lumen::miningShown() ? 2 : 0) |
                                   (lumen::miningActive() ? 4 : 0));
    if (badges != musicShown_) { musicShown_ = badges; if (curHeader_) dirty_.add(lumen::R_HEADER); }
    else if ((badges & 5) && curHeader_ && now - lastEq_ >= 83) { lastEq_ = now; dirty_.add(lumen::R_BADGES); }
    cur_->update(now, dirty_);
    renderDirty();
}

uint32_t UIManager::benchFullFrame(uint8_t runs) {
    if (!cur_ || phase_ != Phase::Idle) return 0;
    if (!runs) runs = 1;
    St7789& d = r_->display();
    r_->paintUs = 0; d.swapUs = 0; d.spiUs = 0;
    uint32_t total = 0;
    for (uint8_t i = 0; i < runs; i++) total += r_->draw(Rect(0, 0, 240, 240), [this](Canvas& c) { paint(c, cur_); });
    benchPaint = r_->paintUs / runs; benchSwap = d.swapUs / runs; benchSpi = d.spiUs / runs;
    return total / runs;
}
