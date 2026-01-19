#pragma once

#include "Event.h"
#include <memory>

namespace DirtSim {
namespace OsManager {

class OperatingSystemManager;
struct EventQueue;

class EventProcessor {
public:
    EventProcessor();

    void processEventsFromQueue(OperatingSystemManager& sm);
    void enqueueEvent(const Event& event);

    bool hasEvents() const;
    size_t queueSize() const;
    void clearQueue();

    std::shared_ptr<EventQueue> eventQueue;
};

} // namespace OsManager
} // namespace DirtSim
