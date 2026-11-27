#pragma once
// Mining page: a little miner with a helmet lamp swings a pickaxe at a rock. The swing speeds up with the
// hash rate, every hit throws sparks, an accepted share pops a DUCO coin into the counter, a rejected one
// makes him say "Oops", and when mining pauses he says why. Below: hash rate, shares, 24 h history,
// earnings and the Bitcoin "lottery". All numbers are real (MinerService and the relay).
// Only the swinging arm, the sparks and the flying coin are redrawn while animating (small dirty rects).
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
    float swing_ = 0;          // 0..1 through one swing (wind-up, strike, rest)
    uint32_t oopsUntil_ = 0;   // speech bubble after a rejected share
    uint32_t seenAcc_ = 0, seenRej_ = 0;
    bool coinWas_ = false;
};
