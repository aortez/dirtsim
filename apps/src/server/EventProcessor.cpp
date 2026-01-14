#include "EventProcessor.h"
#include "StateMachine.h"
#include "core/SynchronizedQueue.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {

struct EventQueue {
    SynchronizedQueue<Event> queue;
};

EventProcessor::EventProcessor() : eventQueue(std::make_shared<EventQueue>())
{}

void EventProcessor::processEventsFromQueue(StateMachine& sm)
{
    {
        const size_t initialQueueDepth = eventQueue->queue.size();
        if (initialQueueDepth > 0) {
            spdlog::debug("EventProcessor: Processing {} queued events", initialQueueDepth);
        }
    }

    while (!eventQueue->queue.empty()) {
        auto event = eventQueue->queue.tryPop();
        if (event.has_value()) {
            size_t remaining = eventQueue->queue.size();
            spdlog::debug(
                "EventProcessor: Processing event: {} (queue depth: {})",
                getEventName(event.value()),
                remaining);
            sm.handleEvent(event.value());
        }
    }
}

void EventProcessor::enqueueEvent(const Event& event)
{
    spdlog::debug("EventProcessor: Enqueuing event: {}", getEventName(event));
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

} // namespace Server
} // namespace DirtSim
