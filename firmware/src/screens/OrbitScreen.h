#pragma once
// CAIDEN OS 2.0 home - concept A "Orbit" (design2/render_concepts.py concept_orbit).
// A 24 h sun-path ring (noon at the top) frames the time: the daylight span arcs over the top
// and the sun (or moon at night) rides the ring at the current time. Inside: temperature gauge
// (left) and the six-band Indian NAQI scale (right). Status in the corners.
//
// Redraw policy (DESIGN_LANGUAGE.md motion): full frame on minute/data/theme change only;
// otherwise small regions - the colon breathes (two dots) and changed digits cross-fade.
#include "../ui/Screen.h"

class OrbitScreen : public Screen {
public:
    const char* name() const override { return "home"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    int lastMin_ = -1;
    uint16_t wxVer_ = 0, airVer_ = 0, netVer_ = 0;
    uint8_t colonStep_ = 0, colonAlpha_ = 255;
    uint8_t bars_ = 0;
    uint8_t mode_ = 0;
    // digit cross-fade
    char shownHH_[4] = "", shownMM_[4] = "", prevHH_[4] = "", prevMM_[4] = "";
    uint32_t fadeAt_ = 0;
    bool fading_ = false;
    float eqPhase_ = 0;
    uint32_t lastEq_ = 0;
    uint16_t brVer_ = 0;
    uint8_t notify_ = 0;
    bool playing_ = false;
    uint8_t mineShown_ = 0;           // pickaxe badge: 1 mining on, 2 hashing
};
