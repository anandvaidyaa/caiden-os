#include "FocusService.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

FocusService g_focus;

// Local calendar day as days since the epoch (no year/leap-year wrap issues).
static int32_t todayIndex() {
    time_t t = time(nullptr);
    if (t < 1700000000) return -1;
    struct tm tm;
    localtime_r(&t, &tm);
    int y = tm.tm_year + 1900, m = tm.tm_mon + 1, d = tm.tm_mday;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

void FocusService::begin() {
    File f = LittleFS.open("/focus.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
        weekDay_ = doc["day"] | -1;
        if (weekDay_ >= 0 && weekDay_ < 400) weekDay_ = -1;   // old yday-based file: re-anchor today
        JsonArray w = doc["w"];
        for (int i = 0; i < 7 && i < int(w.size()); i++) week_[i] = w[i] | 0;
    }
    f.close();
}

void FocusService::save() {
    JsonDocument doc;
    doc["day"] = weekDay_;
    JsonArray w = doc["w"].to<JsonArray>();
    for (uint16_t m : week_) w.add(m);
    File f = LittleFS.open("/focus.tmp", "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
    LittleFS.rename("/focus.tmp", "/focus.json");
}

// Shift the 7-day history when the date changes (also across several missed days).
void FocusService::rollDay() {
    int32_t yd = todayIndex();
    if (yd < 0) return;
    if (weekDay_ < 0) { weekDay_ = yd; return; }
    int32_t gap = yd - weekDay_;
    if (gap <= 0) return;                            // same day, or the clock stepped back: keep history
    if (gap >= 7) { for (auto& m : week_) m = 0; }
    else {
        for (int i = 6; i >= gap; i--) week_[i] = week_[i - gap];
        for (int i = 0; i < gap; i++) week_[i] = 0;
    }
    weekDay_ = yd;
    save();
    ver_++;
}

void FocusService::addFocus(uint32_t seconds) {
    rollDay();
    uint32_t m = week_[0] + (seconds + 30) / 60;
    week_[0] = uint16_t(m > 65535 ? 65535 : m);
    save();
}

uint32_t FocusService::pomoRemaining() const {
    if (phase_ == PomoPhase::Idle) return FOCUS_S;
    if (paused_) return pausedLeft_;
    int32_t left = int32_t(phaseEnd_ - millis()) / 1000;
    return left > 0 ? uint32_t(left) : 0;
}
uint32_t FocusService::swElapsed() const { return (swAcc_ + (swStart_ ? millis() - swStart_ : 0)) / 1000; }
uint32_t FocusService::cdRemaining() const {
    if (!cdEnd_) return 0;
    int32_t left = int32_t(cdEnd_ - millis()) / 1000;
    return left > 0 ? uint32_t(left) : 0;
}
uint16_t FocusService::todayMinutes() const { return week_[0]; }
uint16_t FocusService::dayMinutes(int d) const { return d >= 0 && d < 7 ? week_[d] : 0; }

bool FocusService::command(const char* a, uint32_t s) {
    const uint32_t now = millis();
    if (!strcmp(a, "pomo")) { phase_ = PomoPhase::Focus; paused_ = false; phaseEnd_ = now + FOCUS_S * 1000; session_ = 1; }
    else if (!strcmp(a, "pause")) { if (phase_ != PomoPhase::Idle && !paused_) { pausedLeft_ = pomoRemaining(); paused_ = true; } }
    else if (!strcmp(a, "resume")) { if (paused_) { phaseEnd_ = now + pausedLeft_ * 1000; paused_ = false; } }
    else if (!strcmp(a, "reset")) { phase_ = PomoPhase::Idle; paused_ = false; session_ = 1; }
    else if (!strcmp(a, "sw")) { if (!swStart_) swStart_ = now ? now : 1; }
    else if (!strcmp(a, "swstop")) { if (swStart_) { swAcc_ += now - swStart_; swStart_ = 0; } }
    else if (!strcmp(a, "swreset")) { swStart_ = 0; swAcc_ = 0; }
    else if (!strcmp(a, "cd")) { if (s == 0 || s > 24 * 3600) return false; cdLen_ = s; cdEnd_ = now + s * 1000; }
    else if (!strcmp(a, "cdstop")) { cdEnd_ = 0; cdLen_ = 0; }
    else return false;
    ver_++;
    return true;
}

void FocusService::loop() {
    static uint32_t lastRoll = 0;
    if (millis() - lastRoll > 60000) { lastRoll = millis(); rollDay(); }
    if (phase_ != PomoPhase::Idle && !paused_ && int32_t(millis() - phaseEnd_) >= 0) {
        if (phase_ == PomoPhase::Focus) {
            addFocus(FOCUS_S);
            phase_ = PomoPhase::Break;
            phaseEnd_ = millis() + BREAK_S * 1000;
        } else if (session_ >= 4) {
            phase_ = PomoPhase::Idle;                // all 4 sessions done
            session_ = 1;
        } else {
            session_++;
            phase_ = PomoPhase::Focus;
            phaseEnd_ = millis() + FOCUS_S * 1000;
        }
        ver_++;
    }
    if (cdEnd_ && int32_t(millis() - cdEnd_) >= 0) { cdEnd_ = 0; ver_++; }   // finished
}
