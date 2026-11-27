#pragma once
// Duino-Coin (DUCO-S1) miner that only uses the clock's spare time. A hobby: an ESP8266 earns fractions
// of a cent a day. Off by default; turned on from the settings page with your own Duino-Coin username.
//
// Pool protocol (plain TCP, pool chosen by the relay's /duco.json from Duino-Coin's getPool):
//   pool -> "4.0\n" on connect
//   clock -> "JOB,<user>,ESP8266,<mining key>\n"
//   pool -> "<last hash>,<expected hash>,<difficulty>\n"
//   clock finds n in 0..100*difficulty with SHA1(<last hash> + n) == <expected hash>
//   clock -> "<n>,<hash rate>,CAIDEN OS Miner <version>,<rig>,DUCOID<chip id>,<wallet id>\n"
//   pool -> "GOOD" | "BLOCK" | "BAD[,reason]"
//
// Hashing runs in slices of about 6 ms per main-loop pass, only while the screen isn't animating a
// page change and memory, Wi-Fi and updates allow (see MinerService::loop). The page and the web
// server stay as responsive as before.
#include <Arduino.h>
#include <ESP8266WiFi.h>

class MinerService {
public:
    void loop(bool online);
    void settingsChanged();               // account or on/off changed: reconnect
private:
    enum class St : uint8_t { Idle, Banner, JobWait, Hashing, ResultWait, Backoff };
    void setStatus(uint8_t s, const char* why = "");
    void disconnect(const char* why, uint32_t backoffMs);
    bool readLine(uint32_t now);          // non-blocking: true when line_ holds a full line
    void requestJob();
    bool parseJob();
    void hashSlice(uint32_t now);
    void setDigits();
    void submit(uint32_t nonce);
    void tickStats(uint32_t now);
    WiFiClient cli_;
    St st_ = St::Idle;
    char line_[100];
    uint8_t lineN_ = 0;
    uint32_t waitSince_ = 0, backoffUntil_ = 0;
    // job
    uint32_t pre_[10];          // the 40-character last hash as SHA-1 message words (big-endian)
    uint32_t mid_[5];           // SHA-1 state after rounds 0-9, which only depend on pre_
    uint32_t want_[5];          // the expected hash as words
    char digits_[10];           // nonce_ as ASCII, counted up in place (no formatting per hash)
    uint8_t ndig_ = 1;
    uint32_t nonce_ = 0, maxNonce_ = 0, jobUs_ = 0, jobAt_ = 0;   // jobAt_: millis() when the job arrived
    // stats
    uint32_t hashes_ = 0, rateFrom_ = 0, rateHashes_ = 0, hourFrom_ = 0, hourHashes_ = 0, mineFrom_ = 0;
    uint32_t walletId_ = 0;
    bool wasOn_ = false;
};

extern MinerService g_miner;
