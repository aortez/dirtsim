#pragma once

#include "core/Pimpl.h"
#include "core/ScenarioConfig.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"

#include <functional>
#include <memory>
#include <string>

// Forward declarations (global namespace).
class Timers;
class ScenarioRegistry;

// Forward declarations (DirtSim namespace).
namespace DirtSim {
struct WorldData;

namespace Network {
class WebSocketService;
}

namespace Server {

class Event;
class EventProcessor;
class PeerDiscovery;
class WebSocketServer;
struct QuitApplicationCommand;
struct GetFPSCommand;
struct GetSimStatsCommand;

namespace State {
class Any;
}

class StateMachine : public StateMachineBase, public StateMachineInterface<Event> {
public:
    StateMachine();
    ~StateMachine();

    void mainLoopRun();
    void queueEvent(const Event& event);

    /**
     * @brief Handle an event by dispatching to current state.
     */
    void handleEvent(const Event& event);

    std::string getCurrentStateName() const override;
    void processEvents();

    // Accessor methods for Pimpl members.
    EventProcessor& getEventProcessor();
    const EventProcessor& getEventProcessor() const;

    Network::WebSocketService* getWebSocketService();
    void setWebSocketService(Network::WebSocketService* service);

    /**
     * @brief Setup WebSocketService with command handlers.
     * @param service The WebSocketService to configure (must outlive StateMachine).
     */
    void setupWebSocketService(Network::WebSocketService& service);

    void updateCachedWorldData(const WorldData& data);
    std::shared_ptr<WorldData> getCachedWorldData() const;

    ScenarioRegistry& getScenarioRegistry();
    const ScenarioRegistry& getScenarioRegistry() const;

    Timers& getTimers();
    const Timers& getTimers() const;

    PeerDiscovery& getPeerDiscovery();
    const PeerDiscovery& getPeerDiscovery() const;

    /**
     * @brief Start advertising this server via mDNS/Avahi.
     * @param port The WebSocket port to advertise.
     * @param serviceName Human-readable service name (e.g., hostname).
     */
    void startPeerAdvertisement(uint16_t port, const std::string& serviceName = "sparkle-duck");

    void broadcastRenderMessage(
        const WorldData& data, const std::string& scenario_id, const ScenarioConfig& scenario_config);

    // Default world dimensions optimized for HyperPixel 4.0 (800x480) with icon rail (76px).
    // Available space: 724x480 when panel closed, 474x480 when panel open.
    // 45x30 cells gives ~1.5:1 aspect ratio matching the display area.
    uint32_t defaultWidth = 45;
    uint32_t defaultHeight = 30;

private:
    struct Impl;
    Pimpl<Impl> pImpl;

    /**
     * @brief Transition to a new state.
     * Handles onExit and onEnter lifecycle calls.
     */
    void transitionTo(State::Any newState);

    /**
     * @brief Call onEnter if the state has it.
     */
    template <typename State>
    void callOnEnter(State& state)
    {
        if constexpr (requires { state.onEnter(*this); }) {
            state.onEnter(*this);
        }
    }

    /**
     * @brief Call onExit if the state has it.
     */
    template <typename State>
    void callOnExit(State& state)
    {
        if constexpr (requires { state.onExit(*this); }) {
            state.onExit(*this);
        }
    }

    // Global event handlers (available in all states)
    State::Any onEvent(const QuitApplicationCommand& cmd);
    State::Any onEvent(const GetFPSCommand& cmd);
    State::Any onEvent(const GetSimStatsCommand& cmd);
};

} // namespace Server
} // namespace DirtSim
