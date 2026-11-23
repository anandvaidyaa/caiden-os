#pragma once
// Productivity timers: Pomodoro (25 min focus / 5 min break, 4 sessions), stopwatch and countdown.
// No touch input on this clock, so they are driven from the web page / API:
//   GET /api/focus?a=pomo | pause | resume | reset | sw | swstop | swreset | cd&s=<seconds> | cdstop
// Completed focus minutes are kept per day (last 7 days) in /focus.json.
#include <Arduino.h>

enum class PomoPhase : uint8_t { Idle, Focus, Break };

class FocusService {
public:
    static constexpr uint32_t FOCUS_S = 25 * 60, BREAK_S = 5 * 60;
    void begin();
    void loop();
    bool command(const char* a, uint32_t seconds);   // returns false for an unknown action

    // state for the screen
    PomoPhase phase() const { return phase_; }
    bool pomoRunning() const { return phase_ != PomoPhase::Idle && !paused_; }
    uint32_t pomoRemaining() const;                  // seconds
    uint32_t pomoLength() const { return phase_ == PomoPhase::Break ? BREAK_S : FOCUS_S; }
    uint8_t session() const { return session_; }     // 1..4
    bool swRunning() const { return swStart_ != 0; }
    uint32_t swElapsed() const;                      // seconds
    bool cdActive() const { return cdEnd_ != 0; }
    uint32_t cdRemaining() const;
    uint32_t cdLength() const { return cdLen_; }
    uint16_t todayMinutes() const;
    uint16_t dayMinutes(int daysAgo) const;          // 0 = today
    bool active() const { return phase_ != PomoPhase::Idle || swRunning() || cdActive(); }
    uint16_t version() const { return ver_; }

private:
    void addFocus(uint32_t seconds);
    void save();
    void rollDay();
    PomoPhase phase_ = PomoPhase::Idle;
    bool paused_ = false;
    uint32_t phaseEnd_ = 0, pausedLeft_ = 0;
    uint8_t session_ = 1;
    uint32_t swStart_ = 0, swAcc_ = 0;
    uint32_t cdEnd_ = 0, cdLen_ = 0;
    uint16_t week_[7] = {0};       // minutes, [0] = today
    int32_t weekDay_ = -1;         // days since epoch of week_[0]
    uint16_t ver_ = 0;
};

extern FocusService g_focus;
