#pragma once

#include "ClockEventTypes.h"
#include <map>
#include <string>

namespace DirtSim {

class World;

class EventManager {
public:
    bool isEventActive(ClockEventType type) const;
    size_t getActiveEventCount() const;

    void setCooldown(ClockEventType type, double duration);
    void updateCooldowns(double deltaTime);
    bool isOnCooldown(ClockEventType type) const;

    void updateTimeTracking(const std::string& currentTime, double deltaTime);
    bool hasTimeChangedThisFrame() const;
    bool shouldCheckPeriodicTriggers() const;
    void resetTriggerCheckTimer();

    void addActiveEvent(ClockEventType type, ActiveEvent event);
    void removeActiveEvent(ClockEventType type);
    ActiveEvent* getActiveEvent(ClockEventType type);
    const std::map<ClockEventType, ActiveEvent>& getActiveEvents() const;
    std::map<ClockEventType, ActiveEvent>& getActiveEvents();

    void clear();

private:
    std::map<ClockEventType, ActiveEvent> active_events_;
    std::map<ClockEventType, double> event_cooldowns_;

    double time_since_last_trigger_check_ = 0.0;
    std::string last_trigger_check_time_;
    bool time_changed_this_frame_ = false;
};

} // namespace DirtSim
