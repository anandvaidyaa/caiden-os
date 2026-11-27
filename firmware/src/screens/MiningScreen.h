#pragma once
// Mining page: Duino-Coin hash rate with a spinning orbit, share results, 24 h history, earnings and a
// tongue-in-cheek "Bitcoin lottery" line. All numbers are real (MinerService and the relay).
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
    uint32_t lastSpin_ = 0;
    float phase_ = 0;
    uint8_t chain_ = 0, chainN_ = 0;   // last 8 share results, bit 0 = newest (1 = accepted)
    uint32_t seenShare_ = 0;
    uint32_t seenAcc_ = 0, seenRej_ = 0;
};
