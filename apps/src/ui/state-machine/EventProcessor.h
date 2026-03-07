#pragma once

#include "Event.h"
#include <chrono>
#include <memory>

namespace DirtSim {
namespace Ui {

class StateMachine;
struct EventQueue;

class EventProcessor {
public:
    EventProcessor();

    void processEvent(StateMachine& sm, const Event& event);
    void processEventsFromQueue(StateMachine& sm);
    void enqueueEvent(const Event& event);

    bool hasEvents() const;
    bool waitForEvents(std::chrono::milliseconds timeout);
    size_t queueSize() const;
    void clearQueue();
    void requestYield();

    std::shared_ptr<EventQueue> eventQueue;

private:
    bool yieldRequested_ = false;
};

} // namespace Ui
} // namespace DirtSim
