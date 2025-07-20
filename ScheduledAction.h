// ScheduledAction.h
#pragma once
#include <Arduino.h>
#include <functional>
#include "TimeProviderBase.h"
#include <vector>

// assume you have defined and initialized this somewhere in your sketch:
extern TimeProviderBase* gTimeProvider;

class ScheduledAction {
public:
    /**
     * @param hour   0–23
     * @param minute 0–59
     * @param second 0–59
     * @param action function to call once per day, after the given time
     */
    ScheduledAction(uint8_t hour,
                    uint8_t minute,
                    uint8_t second,
                    std::function<void()> action)
      : _targetSec(hour * 3600UL +
                   minute * 60UL +
                   second),
        _action(std::move(action))
    {}

    /**
     * Call this every loop().  It will:
     *  - read gTimeProvider->getSecondsOfDay()
     *  - detect midnight rollover when the seconds drop
     *  - fire your callback once per day, as soon after the target time
     *    as this method is called
     */
    void loop() {
        if (!gTimeProvider) return;  // guard if not yet set

        int nowSec = gTimeProvider->getSecondsOfDay();

        // 1) Did the clock roll past midnight?  (e.g. from 86399→0)
        if (nowSec < _lastSec) {
            _triggered = false;
        }
        _lastSec = nowSec;

        // 2) If not yet fired today and we've reached/passed target → fire
        if (!_triggered && nowSec >= (int)_targetSec) {
            _action();
            _triggered = true;
        }
    }

    /**  Force re-arm immediately (e.g. after you change the scheduled time) */
    void reset() {
        _triggered = false;
    }

    /**  Has today's callback already run? */
    bool hasFiredToday() const {
        return _triggered;
    }

private:
    const unsigned long   _targetSec;  // seconds since midnight
    std::function<void()> _action;

    int   _lastSec   = 0;    // used to spot the midnight wraparound
    bool  _triggered = false;
};

//just a vector of ScheduledAction objects with one loop() method
class ScheduledActions {
    public:
    void add(ScheduledAction&& action) {
        _actions.push_back(std::move(action));
    }

    void loop() {
        for (auto& action : _actions) {
            action.loop();
        }
    }

    void reset() {
        for (auto& action : _actions) {
            action.reset();
        }
    }
    
    private:
    std::vector<ScheduledAction> _actions;  // store ScheduledAction objects
};