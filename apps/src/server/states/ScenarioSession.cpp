#include "server/states/ScenarioSession.h"

#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/World.h"
#include "core/organisms/OrganismManager.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "server/StateMachine.h"

#include <algorithm>
#include <type_traits>

namespace DirtSim::Server::State {

namespace {

WorldData makeDefaultNesWorldData()
{
    WorldData data;
    data.width = 256;
    data.height = 240;
    data.cells.clear();
    data.colors.data.clear();
    data.scenario_video_frame.reset();
    data.entities.clear();
    data.tree_vision.reset();
    return data;
}

} // namespace

ScenarioSession::~ScenarioSession() = default;
ScenarioSession::ScenarioSession(ScenarioSession&&) noexcept = default;
ScenarioSession& ScenarioSession::operator=(ScenarioSession&&) noexcept = default;

bool ScenarioSession::hasSession() const
{
    return !std::holds_alternative<std::monostate>(session_);
}

bool ScenarioSession::isNesSession() const
{
    return std::holds_alternative<NesWorldSession>(session_);
}

Scenario::EnumType ScenarioSession::getScenarioId() const
{
    return std::visit(
        [](const auto& impl) -> Scenario::EnumType {
            using T = std::decay_t<decltype(impl)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return Scenario::EnumType::Empty;
            }
            else {
                return impl.scenarioId;
            }
        },
        session_);
}

ScenarioKind ScenarioSession::getScenarioKind() const
{
    return std::visit(
        [](const auto& impl) -> ScenarioKind {
            using T = std::decay_t<decltype(impl)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return ScenarioKind::GridWorld;
            }
            else if constexpr (std::is_same_v<T, GridWorldSession>) {
                return ScenarioKind::GridWorld;
            }
            else {
                return ScenarioKind::NesWorld;
            }
        },
        session_);
}

ScenarioConfig ScenarioSession::getScenarioConfig() const
{
    return std::visit(
        [](const auto& impl) -> ScenarioConfig {
            using T = std::decay_t<decltype(impl)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return DirtSim::Config::Empty{};
            }
            else if constexpr (std::is_same_v<T, GridWorldSession>) {
                if (!impl.scenario) {
                    return DirtSim::Config::Empty{};
                }
                return impl.scenario->getConfig();
            }
            else {
                return impl.scenarioConfig;
            }
        },
        session_);
}

const WorldData* ScenarioSession::getWorldData() const
{
    return std::visit(
        [](const auto& impl) -> const WorldData* {
            using T = std::decay_t<decltype(impl)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            }
            else if constexpr (std::is_same_v<T, GridWorldSession>) {
                if (!impl.world) {
                    return nullptr;
                }
                return &impl.world->getData();
            }
            else {
                return &impl.worldData;
            }
        },
        session_);
}

const std::vector<OrganismId>* ScenarioSession::getOrganismGrid() const
{
    return std::visit(
        [](const auto& impl) -> const std::vector<OrganismId>* {
            using T = std::decay_t<decltype(impl)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            }
            else if constexpr (std::is_same_v<T, GridWorldSession>) {
                if (!impl.world) {
                    return nullptr;
                }
                return &impl.world->getOrganismManager().getGrid();
            }
            else {
                return &emptyOrganismGrid();
            }
        },
        session_);
}

const Timers* ScenarioSession::getTimers() const
{
    return std::visit(
        [](const auto& impl) -> const Timers* {
            using T = std::decay_t<decltype(impl)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            }
            else if constexpr (std::is_same_v<T, GridWorldSession>) {
                if (!impl.world) {
                    return nullptr;
                }
                return &impl.world->getTimers();
            }
            else {
                return &impl.timers;
            }
        },
        session_);
}

World* ScenarioSession::getWorld()
{
    auto* impl = std::get_if<GridWorldSession>(&session_);
    if (!impl) {
        return nullptr;
    }
    return impl->world.get();
}

const World* ScenarioSession::getWorld() const
{
    const auto* impl = std::get_if<GridWorldSession>(&session_);
    if (!impl) {
        return nullptr;
    }
    return impl->world.get();
}

ScenarioRunner* ScenarioSession::getScenarioRunner()
{
    auto* impl = std::get_if<GridWorldSession>(&session_);
    if (!impl) {
        return nullptr;
    }
    return impl->scenario.get();
}

const ScenarioRunner* ScenarioSession::getScenarioRunner() const
{
    const auto* impl = std::get_if<GridWorldSession>(&session_);
    if (!impl) {
        return nullptr;
    }
    return impl->scenario.get();
}

const std::vector<OrganismId>& ScenarioSession::emptyOrganismGrid()
{
    static const std::vector<OrganismId> kEmpty;
    return kEmpty;
}

Result<ScenarioSession::GridWorldAccess, ApiError> ScenarioSession::requireGridWorld()
{
    auto* impl = std::get_if<GridWorldSession>(&session_);
    if (!impl || !impl->world) {
        if (isNesSession()) {
            return Result<GridWorldAccess, ApiError>::error(
                ApiError("Not available in NesWorld scenario"));
        }
        return Result<GridWorldAccess, ApiError>::error(ApiError("No world available"));
    }
    DIRTSIM_ASSERT(impl->scenario != nullptr, "ScenarioSession: GridWorld requires a scenario");
    return Result<GridWorldAccess, ApiError>::okay(
        GridWorldAccess{ .world = impl->world.get(), .scenario = impl->scenario.get() });
}

Result<ScenarioSession::NesWorldAccess, ApiError> ScenarioSession::requireNesWorld()
{
    auto* impl = std::get_if<NesWorldSession>(&session_);
    if (!impl || !impl->driver) {
        return Result<NesWorldAccess, ApiError>::error(
            ApiError("Not available in GridWorld scenario"));
    }
    return Result<NesWorldAccess, ApiError>::okay(
        NesWorldAccess{
            .driver = impl->driver.get(),
            .scenarioConfig = &impl->scenarioConfig,
            .timers = &impl->timers,
            .worldData = &impl->worldData,
        });
}

Result<std::monostate, ApiError> ScenarioSession::start(
    StateMachine& dsm,
    Scenario::EnumType scenarioId,
    const ScenarioConfig& scenarioConfig,
    const Vector2s& containerSize)
{
    auto& registry = dsm.getScenarioRegistry();
    const ScenarioMetadata* metadata = registry.getMetadata(scenarioId);
    if (!metadata) {
        return Result<std::monostate, ApiError>::error(
            ApiError(std::string("Scenario not found: ") + std::string(toString(scenarioId))));
    }

    if (metadata->kind == ScenarioKind::NesWorld) {
        return startNesScenario(dsm, scenarioId, scenarioConfig);
    }

    return startGridWorldScenario(dsm, scenarioId, *metadata, scenarioConfig, containerSize);
}

Result<std::monostate, ApiError> ScenarioSession::startGridWorldScenario(
    StateMachine& dsm,
    Scenario::EnumType scenarioId,
    const ScenarioMetadata& metadata,
    const ScenarioConfig& scenarioConfig,
    const Vector2s& containerSize)
{
    uint32_t worldWidth = dsm.defaultWidth;
    uint32_t worldHeight = dsm.defaultHeight;

    if (containerSize.x > 0 && containerSize.y > 0) {
        constexpr int targetCellSize = 16;
        worldWidth = static_cast<uint32_t>(containerSize.x / targetCellSize);
        worldHeight = static_cast<uint32_t>(containerSize.y / targetCellSize);
        worldWidth = std::max(worldWidth, 10u);
        worldHeight = std::max(worldHeight, 10u);
    }
    else if (metadata.requiredWidth > 0 && metadata.requiredHeight > 0) {
        worldWidth = metadata.requiredWidth;
        worldHeight = metadata.requiredHeight;
    }

    LOG_INFO(
        State,
        "Creating World {}x{} (container: {}x{})",
        worldWidth,
        worldHeight,
        containerSize.x,
        containerSize.y);

    auto& registry = dsm.getScenarioRegistry();
    auto scenario = registry.createScenario(scenarioId);
    if (!scenario) {
        return Result<std::monostate, ApiError>::error(ApiError(
            std::string("Scenario factory returned null for: ")
            + std::string(toString(scenarioId))));
    }

    GridWorldSession next;
    next.scenarioId = scenarioId;
    next.world = std::make_unique<World>(worldWidth, worldHeight);
    next.scenario = std::move(scenario);

    next.scenario->setConfig(scenarioConfig, *next.world);
    next.scenario->setup(*next.world);
    next.world->setScenario(next.scenario.get());

    session_ = std::move(next);
    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

Result<std::monostate, ApiError> ScenarioSession::startNesScenario(
    StateMachine& /*dsm*/, Scenario::EnumType scenarioId, const ScenarioConfig& scenarioConfig)
{
    NesWorldSession next;
    next.scenarioId = scenarioId;
    next.scenarioConfig = scenarioConfig;
    next.worldData = makeDefaultNesWorldData();

    next.driver = std::make_unique<NesSmolnesScenarioDriver>(scenarioId);
    auto setResult = next.driver->setConfig(scenarioConfig);
    if (setResult.isError()) {
        return Result<std::monostate, ApiError>::error(ApiError(setResult.errorValue()));
    }

    auto setupResult = next.driver->setup();
    if (setupResult.isError()) {
        return Result<std::monostate, ApiError>::error(ApiError(setupResult.errorValue()));
    }

    session_ = std::move(next);
    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

Result<std::monostate, ApiError> ScenarioSession::reset()
{
    return std::visit(
        [](auto& impl) -> Result<std::monostate, ApiError> {
            using T = std::decay_t<decltype(impl)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return Result<std::monostate, ApiError>::error(ApiError("No scenario session"));
            }
            else if constexpr (std::is_same_v<T, GridWorldSession>) {
                if (!impl.world || !impl.scenario) {
                    return Result<std::monostate, ApiError>::error(ApiError("No scenario session"));
                }
                impl.scenario->reset(*impl.world);
                impl.world->getData().tree_vision.reset();
                impl.world->getData().bones.clear();
                return Result<std::monostate, ApiError>::okay(std::monostate{});
            }
            else {
                if (!impl.driver) {
                    return Result<std::monostate, ApiError>::error(ApiError("No scenario session"));
                }
                const auto resetResult = impl.driver->reset();
                if (resetResult.isError()) {
                    return Result<std::monostate, ApiError>::error(
                        ApiError(resetResult.errorValue()));
                }
                impl.worldData.scenario_video_frame.reset();
                impl.worldData.timestep = 0;
                return Result<std::monostate, ApiError>::okay(std::monostate{});
            }
        },
        session_);
}

} // namespace DirtSim::Server::State
