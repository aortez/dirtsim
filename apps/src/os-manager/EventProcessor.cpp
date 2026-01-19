#include "EventProcessor.h"
#include "OperatingSystemManager.h"
#include "core/LoggingChannels.h"
#include "core/SynchronizedQueue.h"

namespace DirtSim {
namespace OsManager {

struct EventQueue {
    SynchronizedQueue<Event> queue;
};

EventProcessor::EventProcessor() : eventQueue(std::make_shared<EventQueue>())
{}

void EventProcessor::processEventsFromQueue(OperatingSystemManager& sm)
{
    while (!eventQueue->queue.empty()) {
        auto event = eventQueue->queue.tryPop();
        if (event.has_value()) {
            LOG_DEBUG(State, "Processing event: {}", getEventName(event.value()));
            sm.handleEvent(event.value());
        }
    }
}

void EventProcessor::enqueueEvent(const Event& event)
{
    LOG_DEBUG(State, "Enqueuing event: {}", getEventName(event));
    eventQueue->queue.push(event);
}

bool EventProcessor::hasEvents() const
{
    return !eventQueue->queue.empty();
}

size_t EventProcessor::queueSize() const
{
    return eventQueue->queue.size();
}

void EventProcessor::clearQueue()
{
    eventQueue->queue.clear();
}

} // namespace OsManager
} // namespace DirtSim
