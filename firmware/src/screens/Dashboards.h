#pragma once
// CAIDEN OS 2.0 dashboards with device-direct data (Open-Meteo): Weather and Air quality.
// Ported from design2/render_ecosystem.py (scr_weather, scr_air).
// Redraw policy: full frame on data/theme change; the header time region on minute change.
#include "../ui/Screen.h"

class DashboardScreen : public Screen {
public:
    void enter(uint32_t now) override;
    void update(uint32_t now, DirtySet& d) override;
protected:
    int lastMin_ = -1;
    uint16_t wxVer_ = 0, airVer_ = 0, netVer_ = 0;
};

class WeatherScreen : public DashboardScreen {
public:
    const char* name() const override { return "weather"; }
    void render(Canvas& c, const Theme& t) override;
    void background(Canvas& c, const Theme& t) override;
};

class AirScreen : public DashboardScreen {
public:
    const char* name() const override { return "air"; }
    void render(Canvas& c, const Theme& t) override;
    void background(Canvas& c, const Theme& t) override;
};
