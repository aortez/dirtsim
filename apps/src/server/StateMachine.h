#pragma once

#include "core/Pimpl.h"
#include "core/RenderFormat.h"
#include "core/ScenarioConfig.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"
#include "core/organisms/OrganismType.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations (global namespace).
class Timers;
class ScenarioRegistry;

// Forward declarations (DirtSim namespace).
namespace DirtSim {
class GamepadManager;
class GenomeRepository;
struct ServerConfig;
struct WorldData;

namespace Api {
struct TrainingResult;
}

namespace Network {
class WebSocketService;
class WebSocketServiceInterface;
} // namespace Network

namespace Server {

class Event;
class EventProcessor;
class WebSocketServer;
struct QuitApplicationCommand;
struct GetFPSCommand;
struct GetSimStatsCommand;

namespace State {
class Any;
}

class StateMachine : public StateMachineBase, public StateMachineInterface<Event> {
public:
    explicit StateMachine(const std::optional<std::filesystem::path>& dataDir = std::nullopt);
    StateMachine(
        std::unique_ptr<Network::WebSocketServiceInterface> webSocketService,
        const std::optional<std::filesystem::path>& dataDir = std::nullopt);
    ~StateMachine();

    void mainLoopRun();
    void queueEvent(const Event& event);

    void handleEvent(const Event& event);

    std::string getCurrentStateName() const override;
    void processEvents();

    // Accessor methods for Pimpl members.
    EventProcessor& getEventProcessor();
    const EventProcessor& getEventProcessor() const;

    Network::WebSocketServiceInterface* getWebSocketService();
    void setWebSocketService(Network::WebSocketServiceInterface* service);
    void setWebSocketPort(uint16_t port);

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

    GamepadManager& getGamepadManager();
    const GamepadManager& getGamepadManager() const;

    GenomeRepository& getGenomeRepository();
    const GenomeRepository& getGenomeRepository() const;

    void storeTrainingResult(const Api::TrainingResult& result);

    void broadcastRenderMessage(
        const WorldData& data,
        const std::vector<OrganismId>& organism_grid,
        Scenario::EnumType scenario_id,
        const ScenarioConfig& scenario_config);

    void broadcastCommand(const std::string& messageType);
    void broadcastEventData(const std::string& messageType, const std::vector<std::byte>& payload);

    void addSubscriber(const std::string& connectionId, RenderFormat::EnumType format);

    // Default world dimensions optimized for HyperPixel 4.0 (800x480) with icon rail (76px).
    // Available space: 724x480 when panel closed, 474x480 when panel open.
    // 45x30 cells gives ~1.5:1 aspect ratio matching the display area.
    uint32_t defaultWidth = 45;
    uint32_t defaultHeight = 30;

    std::unique_ptr<ServerConfig> serverConfig;

private:
    struct Impl;
    Pimpl<Impl> pImpl;

    void transitionTo(State::Any newState);

    // Global event handlers (available in all states).
    State::Any onEvent(const QuitApplicationCommand& cmd);
    State::Any onEvent(const GetFPSCommand& cmd);
    State::Any onEvent(const GetSimStatsCommand& cmd);
};

} // namespace Server
} // namespace DirtSim
