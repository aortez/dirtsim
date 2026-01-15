#include "EventManager.h"

namespace DirtSim {

bool EventManager::isEventActive(ClockEventType type) const
{
    return active_events_.contains(type);
}

size_t EventManager::getActiveEventCount() const
{
    return active_events_.size();
}

void EventManager::setCooldown(ClockEventType type, double duration)
{
    event_cooldowns_[type] = duration;
}

void EventManager::updateCooldowns(double deltaTime)
{
    for (auto& [type, cooldown] : event_cooldowns_) {
        if (cooldown > 0.0) {
            cooldown -= deltaTime;
        }
    }
}

bool EventManager::isOnCooldown(ClockEventType type) const
{
    auto it = event_cooldowns_.find(type);
    return it != event_cooldowns_.end() && it->second > 0.0;
}

void EventManager::updateTimeTracking(const std::string& currentTime, double deltaTime)
{
    time_changed_this_frame_ = (currentTime != last_trigger_check_time_);
    if (time_changed_this_frame_) {
        last_trigger_check_time_ = currentTime;
    }

    time_since_last_trigger_check_ += deltaTime;
}

bool EventManager::hasTimeChangedThisFrame() const
{
    return time_changed_this_frame_;
}

bool EventManager::shouldCheckPeriodicTriggers() const
{
    return time_since_last_trigger_check_ >= 1.0;
}

void EventManager::resetTriggerCheckTimer()
{
    time_since_last_trigger_check_ = 0.0;
}

void EventManager::addActiveEvent(ClockEventType type, ActiveEvent event)
{
    active_events_[type] = std::move(event);
}

void EventManager::removeActiveEvent(ClockEventType type)
{
    active_events_.erase(type);
}

ActiveEvent* EventManager::getActiveEvent(ClockEventType type)
{
    auto it = active_events_.find(type);
    return it != active_events_.end() ? &it->second : nullptr;
}

const std::map<ClockEventType, ActiveEvent>& EventManager::getActiveEvents() const
{
    return active_events_;
}

std::map<ClockEventType, ActiveEvent>& EventManager::getActiveEvents()
{
    return active_events_;
}

void EventManager::clear()
{
    active_events_.clear();
    event_cooldowns_.clear();
    time_since_last_trigger_check_ = 0.0;
    last_trigger_check_time_.clear();
    time_changed_this_frame_ = false;
}

} // namespace DirtSim
