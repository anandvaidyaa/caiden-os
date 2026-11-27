#include "MinerService.h"
#include <string.h>
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

MinerService g_miner;

// Hashing per main-loop pass. Animations redraw at most every 83 ms and the web server is served between
// slices, so 6 ms keeps the clock responsive while giving mining most of the idle time.
static const uint32_t SLICE_US = 6000;
static const uint16_t MIN_HEAP = 8000;           // pause below this
static const uint16_t DROP_HEAP = 7000;          // disconnect from the pool below this
static const int8_t   MIN_RSSI = -85;
static const uint32_t REPLY_TIMEOUT_MS = 15000;

static uint8_t hexVal(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

// ---- SHA-1 for DUCO-S1. The message is the 40-character last hash plus the nonce's digits (at most 10),
// so it always fits in one 64-byte block. Rounds 0-9 only see the fixed 40 characters: they run once
// per job (the "midstate"), and each nonce runs rounds 10-79. The kernel lives in IRAM, so it doesn't
// wait for the flash cache.
static const uint32_t H0 = 0x67452301, H1 = 0xEFCDAB89, H2 = 0x98BADCFE, H3 = 0x10325476, H4 = 0xC3D2E1F0;
static inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1Mid(const uint32_t pre[10], uint32_t st[5]) {
    uint32_t a = H0, b = H1, c = H2, d = H3, e = H4;
    for (int t = 0; t < 10; t++) {
        const uint32_t tmp = rol(a, 5) + ((b & c) | (~b & d)) + e + 0x5A827999 + pre[t];
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    st[0] = a; st[1] = b; st[2] = c; st[3] = d; st[4] = e;
}

// true when SHA-1(prefix + nonce) == want; w[10..15] hold the nonce words and the padding
static bool IRAM_ATTR __attribute__((noinline)) sha1Match(const uint32_t pre[10], const uint32_t mid[5], const uint32_t tail[6], const uint32_t want[5]) {
    uint32_t w[16];
    for (int i = 0; i < 10; i++) w[i] = pre[i];
    for (int i = 0; i < 6; i++) w[10 + i] = tail[i];
    uint32_t a = mid[0], b = mid[1], c = mid[2], d = mid[3], e = mid[4];
#define SHA1_W(t) (w[(t) & 15] = rol(w[((t) + 13) & 15] ^ w[((t) + 8) & 15] ^ w[((t) + 2) & 15] ^ w[(t) & 15], 1))
#define SHA1_R(f, k, x) do { const uint32_t tmp = rol(a, 5) + (f) + e + (k) + (x); e = d; d = c; c = rol(b, 30); b = a; a = tmp; } while (0)
    for (int t = 10; t < 16; t++) SHA1_R((b & c) | (~b & d), 0x5A827999, w[t]);
    for (int t = 16; t < 20; t++) SHA1_R((b & c) | (~b & d), 0x5A827999, SHA1_W(t));
    for (int t = 20; t < 40; t++) SHA1_R(b ^ c ^ d, 0x6ED9EBA1, SHA1_W(t));
    for (int t = 40; t < 60; t++) SHA1_R((b & c) | (b & d) | (c & d), 0x8F1BBCDC, SHA1_W(t));
    for (int t = 60; t < 80; t++) SHA1_R(b ^ c ^ d, 0xCA62C1D6, SHA1_W(t));
#undef SHA1_R
#undef SHA1_W
    return a + H0 == want[0] && b + H1 == want[1] && c + H2 == want[2] && d + H3 == want[3] && e + H4 == want[4];
}

// the message tail for the current digits: digits, 0x80, zeros, length in bits (big-endian words 10..15)
static void sha1Tail(const char* dg, uint8_t n, uint32_t tail[6]) {
    uint8_t b[24] = {0};
    memcpy(b, dg, n);
    b[n] = 0x80;
    for (int i = 0; i < 4; i++) tail[i] = uint32_t(b[4 * i]) << 24 | uint32_t(b[4 * i + 1]) << 16 | uint32_t(b[4 * i + 2]) << 8 | b[4 * i + 3];
    tail[4] = 0;
    tail[5] = uint32_t(40 + n) * 8;
}

void MinerService::setStatus(uint8_t s, const char* why) {
    MiningData& m = g_state.mine;
    const MineStatus ns = MineStatus(s);
    if (m.status != ns || strcmp(m.why, why)) {
        m.status = ns;
        snprintf_P(m.why, sizeof m.why, PSTR("%s"), why);
        m.version++;
    }
}

void MinerService::disconnect(const char* why, uint32_t backoffMs) {
    if (cli_.connected()) cli_.stop();
    st_ = backoffMs ? St::Backoff : St::Idle;
    backoffUntil_ = millis() + backoffMs;
    lineN_ = 0;
    if (why && why[0]) Serial.printf_P(PSTR("[mine] %s\n"), why);
}

void MinerService::settingsChanged() {
    disconnect("settings changed", 0);
    g_state.mine.version++;
}

bool MinerService::readLine(uint32_t now) {
    while (cli_.available()) {
        const int ch = cli_.read();
        if (ch < 0) break;
        if (ch == '\n') { line_[lineN_] = 0; lineN_ = 0; return true; }
        if (ch != '\r' && lineN_ < sizeof line_ - 1) line_[lineN_++] = char(ch);
    }
    if (now - waitSince_ > REPLY_TIMEOUT_MS) disconnect("pool did not answer", 10000);
    else if (!cli_.connected()) disconnect("pool closed the connection", 10000);
    return false;
}

void MinerService::requestJob() {
    const Config& c = g_config.get();
    cli_.printf_P(PSTR("JOB,%s,ESP8266,%s\n"), c.ducoUser, c.ducoKey[0] ? c.ducoKey : "None");
    st_ = St::JobWait;
    waitSince_ = millis();
}

bool MinerService::parseJob() {   // "<40 hex last hash>,<40 hex expected>,<difficulty>"
    char* c1 = strchr(line_, ',');
    if (!c1 || c1 - line_ != 40) return false;
    char* c2 = strchr(c1 + 1, ',');
    if (!c2 || c2 - c1 - 1 != 40) return false;
    const long diff = atol(c2 + 1);
    if (diff <= 0 || diff > 100000) return false;
    for (int i = 0; i < 40; i++)
        if (!isxdigit(uint8_t(c1[1 + i]))) return false;
    for (int i = 0; i < 5; i++) {
        uint32_t v = 0;
        for (int k = 0; k < 8; k++) v = v << 4 | hexVal(c1[1 + 8 * i + k]);
        want_[i] = v;
    }
    for (int i = 0; i < 10; i++)
        pre_[i] = uint32_t(uint8_t(line_[4 * i])) << 24 | uint32_t(uint8_t(line_[4 * i + 1])) << 16 |
                  uint32_t(uint8_t(line_[4 * i + 2])) << 8 | uint8_t(line_[4 * i + 3]);
    sha1Mid(pre_, mid_);
    nonce_ = 0;
    setDigits();
    maxNonce_ = uint32_t(diff) * 100 + 1;
    jobUs_ = 0;
    jobAt_ = waitSince_;   // when the job was requested: includes the round trip, like the pool's clock
    if (g_state.mine.diff != uint16_t(diff)) { g_state.mine.diff = uint16_t(diff); g_state.mine.version++; }
    return true;
}

void MinerService::setDigits() {
    ndig_ = uint8_t(snprintf_P(digits_, sizeof digits_, PSTR("%lu"), (unsigned long)nonce_));
}

void MinerService::hashSlice(uint32_t now) {
    (void)now;
    const uint32_t t0 = micros();
    uint32_t tail[6];
    sha1Tail(digits_, ndig_, tail);
    do {
        for (uint8_t k = 0; k < 32 && nonce_ < maxNonce_; k++) {
            if (sha1Match(pre_, mid_, tail, want_)) {
                hashes_++;
                jobUs_ += micros() - t0;
                submit(nonce_);
                return;
            }
            hashes_++;
            nonce_++;
            // count up in ASCII: "199" -> "200"; a new digit (e.g. "999" -> "1000") rebuilds the string
            int i = ndig_ - 1;
            while (i >= 0 && digits_[i] == '9') digits_[i--] = '0';
            if (i >= 0) digits_[i]++;
            else setDigits();
            sha1Tail(digits_, ndig_, tail);
        }
        if (nonce_ >= maxNonce_) {           // no answer in range (the pool's job was odd): get another
            jobUs_ += micros() - t0;
            requestJob();
            return;
        }
    } while (micros() - t0 < SLICE_US);
    jobUs_ += micros() - t0;
}

void MinerService::submit(uint32_t nonce) {
    const Config& c = g_config.get();
    // The pool checks the reported rate against its own clock (job sent -> answer received) and rejects
    // shares whose rate it can't have measured ("BAD,Modified hashrate"). Hashing in slices makes the
    // time spent hashing much shorter than that, so report the wall-clock rate since the job arrived.
    const uint32_t wallMs = millis() - jobAt_ + 1;
    const float rate = nonce * 1000.f / float(wallMs);
    cli_.printf_P(PSTR("%lu,%.2f,CAIDEN OS Miner " CAIDEN_FW_VERSION ",%s,DUCOID%08X,%lu\n"), (unsigned long)nonce, rate,
                c.ducoRig[0] ? c.ducoRig : "caiden", unsigned(ESP.getChipId()), (unsigned long)walletId_);
    st_ = St::ResultWait;
    waitSince_ = millis();
}

void MinerService::tickStats(uint32_t now) {
    MiningData& m = g_state.mine;
    if (!rateFrom_) { rateFrom_ = now; rateHashes_ = hashes_; }
    if (now - rateFrom_ >= 5000) {          // effective rate over the last 5 s of wall time
        const float hr = (hashes_ - rateHashes_) * 1000.f / float(now - rateFrom_);
        rateFrom_ = now; rateHashes_ = hashes_;
        if (fabsf(hr - m.hr) >= 1) { m.hr = hr; m.version++; }
        static uint32_t lastLog = 0;
        if (now - lastLog >= 60000) {        // once a minute in the USB log, for checking speed and shares
            lastLog = now;
            Serial.printf_P(PSTR("[mine] %.0f H/s, %lu accepted, %lu rejected, diff %u\n"), m.hr,
                            (unsigned long)m.acc, (unsigned long)m.rej, unsigned(m.diff));
        }
    }
    if (!hourFrom_) { hourFrom_ = now; hourHashes_ = hashes_; }
    if (now - hourFrom_ >= 3600000UL) {
        const uint32_t avg = (hashes_ - hourHashes_) / 3600;
        if (m.histN >= 24) { memmove(m.hist, m.hist + 1, sizeof m.hist[0] * 23); m.histN = 23; }
        m.hist[m.histN++] = uint16_t(avg > 65535 ? 65535 : avg);
        hourFrom_ = now; hourHashes_ = hashes_;
        m.version++;
    }
    if (m.status == MineStatus::Mining) {
        if (mineFrom_) m.mineMs += now - mineFrom_;
        mineFrom_ = now;
    } else {
        mineFrom_ = 0;
    }
}

void MinerService::loop(bool online) {
    const uint32_t now = millis();
    const Config& c = g_config.get();
    MiningData& m = g_state.mine;
    if (!walletId_) walletId_ = ESP.random() % 2811;

    if (!c.mineOn) {
        if (wasOn_) { disconnect("mining turned off", 0); wasOn_ = false; m.hr = 0; }
        setStatus(uint8_t(MineStatus::Off));
        return;
    }
    if (!wasOn_) { wasOn_ = true; rateFrom_ = 0; hourFrom_ = 0; }
    tickStats(now);
    if (!c.ducoUser[0]) { setStatus(uint8_t(MineStatus::NoAccount)); return; }
    if (!online) { if (cli_.connected()) disconnect("Wi-Fi down", 0); setStatus(uint8_t(MineStatus::Paused), "No Wi-Fi"); return; }
    if (g_state.ota.phase != OtaPhase::Idle) { setStatus(uint8_t(MineStatus::Paused), "Updating"); return; }
    const uint32_t heap = ESP.getFreeHeap();
    if (heap < DROP_HEAP && cli_.connected()) disconnect("low memory", 30000);
    if (heap < MIN_HEAP) { setStatus(uint8_t(MineStatus::Paused), "Low memory"); return; }
    if (g_state.net.rssi < MIN_RSSI && g_state.net.rssi != 0) { setStatus(uint8_t(MineStatus::Paused), "Weak Wi-Fi"); return; }
    if (!m.poolIp[0] || !m.poolPort) { setStatus(uint8_t(MineStatus::WaitingPool)); return; }

    switch (st_) {
    case St::Backoff:
        if (int32_t(now - backoffUntil_) < 0) return;
        st_ = St::Idle;
        // fall through
    case St::Idle: {
        setStatus(uint8_t(MineStatus::Connecting));
        cli_.setTimeout(3000);
        IPAddress ip;
        if (!ip.fromString(m.poolIp) || !cli_.connect(ip, m.poolPort)) {
            setStatus(uint8_t(MineStatus::Error), "Pool unreachable");
            disconnect("pool unreachable", 30000);
            return;
        }
        cli_.setNoDelay(true);
        st_ = St::Banner;
        waitSince_ = now;
        lineN_ = 0;
        return;
    }
    case St::Banner:
        if (readLine(now)) requestJob();       // "4.0": the server version
        return;
    case St::JobWait:
        if (!readLine(now)) return;
        if (!parseJob()) {
            Serial.printf_P(PSTR("[mine] unexpected job: %.40s\n"), line_);
            setStatus(uint8_t(MineStatus::Error), "Pool refused the job");
            disconnect("bad job", 60000);
            return;
        }
        st_ = St::Hashing;
        setStatus(uint8_t(MineStatus::Mining));
        return;
    case St::Hashing:
        hashSlice(now);
        return;
    case St::ResultWait:
        if (!readLine(now)) return;
        m.lastShareAt = now;
        m.lastGood = !strncmp(line_, "GOOD", 4) || !strncmp(line_, "BLOCK", 5);
        if (m.lastGood) m.acc++;
        else { m.rej++; Serial.printf_P(PSTR("[mine] share rejected: %.40s\n"), line_); }
        m.version++;
        requestJob();
        return;
    }
}
