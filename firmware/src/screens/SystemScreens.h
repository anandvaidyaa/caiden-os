#pragma once
// Boot splash and Wi-Fi setup screens.
#include "../ui/Screen.h"

class BootScreen : public Screen {
public:
    const char* name() const override { return "boot"; }
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t, DirtySet&) override {}
};

// Shown while a wireless update streams in, then "Restarting", or the failure for a few seconds.
class UpdateScreen : public Screen {
public:
    const char* name() const override { return "update"; }
    void enter(uint32_t) override { shownPct_ = 255; }
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
private:
    uint8_t shownPct_ = 255, shownPhase_ = 255;
};

class SetupScreen : public Screen {
public:
    const char* name() const override { return "setup"; }
    void enter(uint32_t now) override { start_ = now; }
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
private:
    uint32_t start_ = 0, last_ = 0;
    float pulse_ = 0;
};
