#pragma once
// Counts the devices on the clock's own network (the "Home network" page), with no PC and no relay.
// Every 5 minutes it asks each address of the clock's /24 subnet "who has this IP?" (ARP), a few at
// a time, and collects the answers from lwIP's ARP table. Phones and laptops answer ARP even while
// their screens are off, so this counts what is connected, Wi-Fi and wired alike. Nothing is sent
// off the network: results stay on the clock.
#include <Arduino.h>

class LanScanService {
public:
    void loop(bool online);
private:
    void startScan();
    void step(uint32_t now);
    void harvest();
    void finish();
    struct Found { uint8_t ip; uint8_t mac[3]; };   // last address byte, last 3 MAC bytes
    static const uint8_t MAX_FOUND = 64, SEEN_MAX = 64;
    Found found_[MAX_FOUND];
    uint8_t foundN_ = 0;
    uint8_t seen_[SEEN_MAX][3];                       // MAC tails seen in earlier scans (for "new")
    uint8_t seenN_ = 0;
    bool scanning_ = false, firstDone_ = false;
    uint8_t nextHost_ = 1, prefix_[3] = {0, 0, 0}, self_ = 0, gw_ = 0;
    uint32_t nextScan_ = 20000, batchAt_ = 0;
    bool batchSent_ = false;
};

extern LanScanService g_lan;
