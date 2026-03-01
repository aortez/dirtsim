#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/ScenarioId.h"
#include "core/Timers.h"
#include "core/Vector2.h"
#include "core/WorldData.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "server/api/ApiError.h"

#include <memory>
#include <variant>
#include <vector>

namespace DirtSim {
class ScenarioRunner;
class World;
} // namespace DirtSim

namespace DirtSim::Server {
class StateMachine;
} // namespace DirtSim::Server

namespace DirtSim::Server::State {

class ScenarioSession final {
public:
    struct GridWorldAccess {
        World* world = nullptr;
        ScenarioRunner* scenario = nullptr;
    };

    struct NesWorldAccess {
        NesSmolnesScenarioDriver* driver = nullptr;
        ScenarioConfig* scenarioConfig = nullptr;
        Timers* timers = nullptr;
        WorldData* worldData = nullptr;
    };

    ScenarioSession() = default;
    ~ScenarioSession();

    ScenarioSession(const ScenarioSession&) = delete;
    ScenarioSession& operator=(const ScenarioSession&) = delete;
    ScenarioSession(ScenarioSession&&) noexcept;
    ScenarioSession& operator=(ScenarioSession&&) noexcept;

    bool hasSession() const;
    bool isNesSession() const;

    Scenario::EnumType getScenarioId() const;
    ScenarioKind getScenarioKind() const;

    ScenarioConfig getScenarioConfig() const;

    const WorldData* getWorldData() const;
    const std::vector<OrganismId>* getOrganismGrid() const;
    const Timers* getTimers() const;

    Result<GridWorldAccess, ApiError> requireGridWorld();
    Result<NesWorldAccess, ApiError> requireNesWorld();

    World* getWorld();
    const World* getWorld() const;
    ScenarioRunner* getScenarioRunner();
    const ScenarioRunner* getScenarioRunner() const;

    Result<std::monostate, ApiError> start(
        StateMachine& dsm,
        Scenario::EnumType scenarioId,
        const ScenarioConfig& scenarioConfig,
        const Vector2s& containerSize);

    Result<std::monostate, ApiError> reset();

private:
    struct GridWorldSession {
        Scenario::EnumType scenarioId = Scenario::EnumType::Empty;
        std::unique_ptr<World> world;
        std::unique_ptr<ScenarioRunner> scenario;
    };

    struct NesWorldSession {
        Scenario::EnumType scenarioId = Scenario::EnumType::Empty;
        ScenarioConfig scenarioConfig;
        WorldData worldData;
        Timers timers;
        std::unique_ptr<NesSmolnesScenarioDriver> driver;
    };

    using SessionImpl = std::variant<std::monostate, GridWorldSession, NesWorldSession>;

    static const std::vector<OrganismId>& emptyOrganismGrid();

    Result<std::monostate, ApiError> startGridWorldScenario(
        StateMachine& dsm,
        Scenario::EnumType scenarioId,
        const ScenarioMetadata& metadata,
        const ScenarioConfig& scenarioConfig,
        const Vector2s& containerSize);

    Result<std::monostate, ApiError> startNesScenario(
        StateMachine& dsm, Scenario::EnumType scenarioId, const ScenarioConfig& scenarioConfig);

    SessionImpl session_;
};

} // namespace DirtSim::Server::State
