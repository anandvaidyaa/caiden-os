#pragma once
// Mining page v2, "crystal mine" (designed in the simulator, scratch mine2.py, then ported 1:1):
// a cave with a hanging lantern, rails and a mine cart; a miner with a lamp beam swings a pickaxe at a
// glowing crystal ore (motion-blur ghosts, sparks and rock chips on impact, crystals flash). Each accepted
// share flies a coin with a trail into the cart (which bounces, "+1" floats up; the pile grows over the
// session); a rejected share makes him say "Oops"; paused, he sits down and dozes ("Zzz"). Below, a glass
// card: gradient hash-rate digits, an acceptance ring, a filled 24 h sparkline and a ticker alternating
// earnings and the Bitcoin odds. All numbers are real (MinerService and the relay).
// Only the miner, the ore, the coin's path and the cart are redrawn while animating (~7 fps).
#include "../ui/Screen.h"

class MiningScreen : public Screen {
public:
    const char* name() const override { return "mining"; }
    void enter(uint32_t now) override;
    void render(Canvas& c, const Theme& t) override;
    void update(uint32_t now, DirtySet& d) override;
    void background(Canvas& c, const Theme& t) override;
private:
    uint16_t ver_ = 0;
    int lastMin_ = -1;
    uint32_t lastFrame_ = 0, cycleFrom_ = 0;
    float swing_ = 0;             // 0..1 through one swing (wind-up, strike, rest)
    uint8_t ticker_ = 255;        // which ticker line is shown
    bool coinWas_ = false, cartWas_ = false, bubbleWas_ = false;
};
