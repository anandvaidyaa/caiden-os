#include "MinerService.h"
#include <string.h>
#include "../configuration/ConfigurationManager.h"
#include "../state/AppState.h"

MinerService g_miner;

static const uint32_t SLICE_US = 3000;           // hashing per main-loop pass
static const uint16_t MIN_HEAP = 8000;           // pause below this
static const uint16_t DROP_HEAP = 7000;          // disconnect from the pool below this
static const int8_t   MIN_RSSI = -85;
static const uint32_t REPLY_TIMEOUT_MS = 15000;

static uint8_t hexVal(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

void MinerService::setStatus(uint8_t s, const char* why) {
    MiningData& m = g_state.mine;
    const MineStatus ns = MineStatus(s);
    if (m.status != ns || strcmp(m.why, why)) {
        m.status = ns;
        snprintf(m.why, sizeof m.why, "%s", why);
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
    for (int i = 0; i < 20; i++) {
        const char a = c1[1 + 2 * i], b = c1[2 + 2 * i];
        if (!isxdigit(uint8_t(a)) || !isxdigit(uint8_t(b))) return false;
        want_[i] = uint8_t(hexVal(a) << 4 | hexVal(b));
    }
    br_sha1_init(&base_);
    br_sha1_update(&base_, line_, 40);
    nonce_ = 0;
    maxNonce_ = uint32_t(diff) * 100 + 1;
    jobUs_ = 0;
    jobAt_ = waitSince_;   // when the job was requested: includes the round trip, like the pool's clock
    if (g_state.mine.diff != uint16_t(diff)) { g_state.mine.diff = uint16_t(diff); g_state.mine.version++; }
    return true;
}

void MinerService::hashSlice(uint32_t now) {
    (void)now;
    const uint32_t t0 = micros();
    br_sha1_context ctx;
    uint8_t out[20];
    char num[11];
    do {
        for (uint8_t k = 0; k < 8 && nonce_ < maxNonce_; k++, nonce_++) {
            memcpy(&ctx, &base_, sizeof ctx);
            const int n = snprintf(num, sizeof num, "%lu", (unsigned long)nonce_);
            br_sha1_update(&ctx, num, size_t(n));
            br_sha1_out(&ctx, out);
            hashes_++;
            if (out[0] == want_[0] && !memcmp(out, want_, 20)) {
                jobUs_ += micros() - t0;
                submit(nonce_);
                return;
            }
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
