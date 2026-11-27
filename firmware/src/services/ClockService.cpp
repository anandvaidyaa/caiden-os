#include "ClockService.h"
#include "../state/AppState.h"

ClockService g_clock;

void ClockService::begin(const char* tz) {
    // SNTP runs in the background; sync interval is the core default (1 h).
    configTime(tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

bool ClockService::valid() const { return time(nullptr) > 1700000000; }

void ClockService::loop() {
    bool v = valid();
    if (v && !g_state.timeValid) Serial.println(F("[clock] time synced"));
    g_state.timeValid = v;
}

bool ClockService::now(struct tm& out) const {
    time_t t = time(nullptr);
    if (t < 1700000000) return false;
    localtime_r(&t, &out);
    return true;
}

void ClockService::hhmm(char* hh, char* mm, bool h24, bool& am) const {
    struct tm t;
    if (!now(t)) { strcpy(hh, "--"); strcpy(mm, "--"); am = true; return; }
    am = t.tm_hour < 12;
    int h = t.tm_hour;
    if (!h24) { h %= 12; if (h == 0) h = 12; sprintf(hh, "%d", h); }
    else sprintf(hh, "%02d", h);
    sprintf(mm, "%02d", t.tm_min);
}

void ClockService::dateLabel(char* out, size_t n) const {
    struct tm t;
    if (!now(t)) { snprintf_P(out, n, PSTR("SYNCING TIME")); return; }
    strftime(out, n, "%a %d %b", &t);
    for (char* p = out; *p; p++) *p = toupper(uint8_t(*p));
}

void ClockService::timeShort(char* out, size_t n, bool h24) const {
    char hh[4], mm[4]; bool am;
    hhmm(hh, mm, h24, am);
    snprintf_P(out, n, PSTR("%s:%s"), hh, mm);
}
