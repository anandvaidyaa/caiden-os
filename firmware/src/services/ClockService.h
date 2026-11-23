#pragma once
// NTP time with a POSIX TZ (default IST). The ESP keeps time from its RTC timer
// between syncs, so the clock keeps running through internet outages.
#include <Arduino.h>
#include <time.h>

class ClockService {
public:
    void begin(const char* posixTz);
    void loop();
    bool valid() const;
    bool now(struct tm& out) const;                       // local time
    // "10:42" (no leading zero in 12 h), am = true when before noon
    void hhmm(char* hh, char* mm, bool h24, bool& am) const;
    void dateLabel(char* out, size_t n) const;            // "FRI 20 NOV"
    void timeShort(char* out, size_t n, bool h24) const;  // "10:42"
};

extern ClockService g_clock;
