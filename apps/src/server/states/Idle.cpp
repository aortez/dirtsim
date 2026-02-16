#include "State.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/ServerConfig.h"
#include "server/StateMachine.h"
#include "server/api/ApiError.h"
#include <algorithm>
#include <cassert>
#include <thread>

namespace DirtSim {
namespace Server {
namespace State {

namespace {

ScenarioConfig buildScenarioConfigForRun(StateMachine& dsm, Scenario::EnumType scenarioId)
{
    ScenarioConfig scenarioConfig = makeDefaultConfig(scenarioId);
    if (dsm.serverConfig && getScenarioId(dsm.serverConfig->startupConfig) == scenarioId) {
        scenarioConfig = dsm.serverConfig->startupConfig;
    }

    if (auto* clockConfig = std::get_if<Config::Clock>(&scenarioConfig)) {
        clockConfig->timezoneIndex = static_cast<uint8_t>(std::clamp(
            dsm.getUserSettings().timezoneIndex,
            0,
            static_cast<int>(ClockScenario::TIMEZONES.size()) - 1));
    }

    return scenarioConfig;
}

bool isBestGenomeCompatibleForPopulation(
    const GenomeMetadata& metadata, OrganismType organismType, const PopulationSpec& populationSpec)
{
    if (!metadata.organismType.has_value() || metadata.organismType.value() != organismType) {
        return false;
    }

    if (metadata.scenarioId != populationSpec.scenarioId) {
        return false;
    }

    if (!metadata.brainKind.has_value() || metadata.brainKind.value() != populationSpec.brainKind) {
        return false;
    }

    if (metadata.brainVariant.value_or("") != populationSpec.brainVariant.value_or("")) {
        return false;
    }

    return true;
}

std::optional<ApiError> validateTrainingConfig(
    const Api::EvolutionStart::Command& command,
    ScenarioRegistry& registry,
    GenomeRepository& repo,
    TrainingSpec& outSpec,
    int& outPopulationSize,
    bool& outWarmSeedInjected)
{
    outWarmSeedInjected = false;
    outSpec.scenarioId = command.scenarioId;
    outSpec.organismType = command.organismType;
    outSpec.population = command.population;

    if (outSpec.population.empty()) {
        if (command.evolution.populationSize <= 0) {
            return ApiError("populationSize must be > 0 when population is empty");
        }

        PopulationSpec defaultSpec;
        defaultSpec.scenarioId = outSpec.scenarioId;
        defaultSpec.count = command.evolution.populationSize;

        switch (outSpec.organismType) {
            case OrganismType::TREE:
                defaultSpec.brainKind = TrainingBrainKind::NeuralNet;
                defaultSpec.randomCount = defaultSpec.count;
                break;
            case OrganismType::DUCK:
                defaultSpec.brainKind = TrainingBrainKind::Random;
                break;
            case OrganismType::GOOSE:
                defaultSpec.brainKind = TrainingBrainKind::Random;
                break;
            default:
                return ApiError("Unsupported organismType for training");
        }

        outSpec.population.push_back(defaultSpec);
    }

    TrainingBrainRegistry brainRegistry = TrainingBrainRegistry::createDefault();
    std::optional<GenomeId> bestSeedId;
    std::optional<GenomeMetadata> bestSeedMetadata;
    if (command.resumePolicy == TrainingResumePolicy::WarmFromBest) {
        auto bestId = repo.getBestId();
        if (bestId.has_value()) {
            auto metadata = repo.getMetadata(bestId.value());
            if (metadata.has_value()) {
                bestSeedId = bestId;
                bestSeedMetadata = metadata;
            }
        }
    }

    outPopulationSize = 0;
    for (auto& spec : outSpec.population) {
        const ScenarioMetadata* metadata = registry.getMetadata(spec.scenarioId);
        if (!metadata) {
            return ApiError(
                std::string("Scenario not found: ") + std::string(toString(spec.scenarioId)));
        }
        if (spec.count <= 0) {
            return ApiError("Population entry count must be > 0");
        }
        if (spec.brainKind.empty()) {
            return ApiError("Population entry brainKind must not be empty");
        }

        const std::string variant = spec.brainVariant.value_or("");
        const BrainRegistryEntry* entry =
            brainRegistry.find(outSpec.organismType, spec.brainKind, variant);
        if (!entry) {
            std::string message = "Brain kind not registered: " + spec.brainKind;
            if (!variant.empty()) {
                message += " (" + variant + ")";
            }
            return ApiError(message);
        }

        if (entry->requiresGenome) {
            if (!outWarmSeedInjected && bestSeedId.has_value() && bestSeedMetadata.has_value()
                && spec.randomCount > 0 && spec.count > 0
                && isBestGenomeCompatibleForPopulation(
                    bestSeedMetadata.value(), outSpec.organismType, spec)
                && std::find(spec.seedGenomes.begin(), spec.seedGenomes.end(), bestSeedId.value())
                    == spec.seedGenomes.end()) {
                spec.seedGenomes.push_back(bestSeedId.value());
                spec.randomCount -= 1;
                outWarmSeedInjected = true;
            }

            if (spec.randomCount < 0) {
                return ApiError("randomCount must be >= 0");
            }
            const int seedCount = static_cast<int>(spec.seedGenomes.size());
            if (spec.count != seedCount + spec.randomCount) {
                return ApiError("Population count must match seedGenomes + randomCount");
            }

            for (const auto& id : spec.seedGenomes) {
                if (id.isNil()) {
                    return ApiError("Seed genome ID is nil");
                }
                if (!repo.exists(id)) {
                    return ApiError("Seed genome not found: " + id.toShortString());
                }
            }
        }
        else {
            if (!spec.seedGenomes.empty()) {
                return ApiError("seedGenomes are not allowed for this brain kind");
            }
            if (spec.randomCount != 0) {
                return ApiError("randomCount must be 0 for this brain kind");
            }
        }

        outPopulationSize += spec.count;
    }

    if (!outSpec.population.empty()) {
        outSpec.scenarioId = outSpec.population.front().scenarioId;
    }

    if (outPopulationSize <= 0) {
        return ApiError("Population size must be > 0");
    }

    return std::nullopt;
}

int resolveParallelEvaluations(int requested, int populationSize)
{
    int resolved = requested;
    if (resolved <= 0) {
        const unsigned int cores = std::thread::hardware_concurrency();
        resolved = cores > 0 ? static_cast<int>(cores) : 1;
    }

    if (resolved < 1) {
        resolved = 1;
    }
    if (populationSize > 0 && resolved > populationSize) {
        resolved = populationSize;
    }
    return resolved;
}

} // namespace

void Idle::onEnter(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Server ready, waiting for commands (no active World)");
    // Note: World is owned by SimRunning state, not StateMachine.
}

void Idle::onExit(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Exiting");
}

State::Any Idle::onEvent(const Api::EvolutionStart::Cwc& cwc, StateMachine& dsm)
{
    LOG_INFO(State, "EvolutionStart command received");

    TrainingSpec trainingSpec;
    int populationSize = 0;
    bool warmSeedInjected = false;
    auto error = validateTrainingConfig(
        cwc.command,
        dsm.getScenarioRegistry(),
        dsm.getGenomeRepository(),
        trainingSpec,
        populationSize,
        warmSeedInjected);
    if (error.has_value()) {
        LOG_WARN(State, "EvolutionStart rejected: {}", error->message);
        cwc.sendResponse(Api::EvolutionStart::Response::error(error.value()));
        return Idle{};
    }

    Evolution newState;
    newState.evolutionConfig = cwc.command.evolution;
    newState.mutationConfig = cwc.command.mutation;
    newState.trainingSpec = std::move(trainingSpec);
    newState.evolutionConfig.populationSize = populationSize;
    newState.evolutionConfig.maxParallelEvaluations =
        resolveParallelEvaluations(cwc.command.evolution.maxParallelEvaluations, populationSize);

    LOG_INFO(
        State,
        "Starting evolution: population={}, generations={}, scenario={}, organism_type={}",
        newState.evolutionConfig.populationSize,
        cwc.command.evolution.maxGenerations,
        toString(newState.trainingSpec.scenarioId),
        static_cast<int>(newState.trainingSpec.organismType));
    LOG_INFO(
        State,
        "Evolution: max parallel evaluations = {}",
        newState.evolutionConfig.maxParallelEvaluations);
    if (cwc.command.resumePolicy == TrainingResumePolicy::WarmFromBest) {
        if (warmSeedInjected) {
            LOG_INFO(State, "Evolution: Warm resume injected repository best genome into seeds");
        }
        else {
            LOG_INFO(State, "Evolution: Warm resume found no compatible repository best genome");
        }
    }

    cwc.sendResponse(Api::EvolutionStart::Response::okay({ .started = true }));
    return newState;
}

State::Any Idle::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state (Shutdown.onEnter will set shouldExit flag).
    return Shutdown{};
}

State::Any Idle::onEvent(const Api::SimRun::Cwc& cwc, StateMachine& dsm)
{
    assert(dsm.serverConfig && "serverConfig must be loaded");

    // Use scenario_id from command if provided, otherwise fall back to user settings.
    Scenario::EnumType scenarioId = cwc.command.scenario_id.has_value()
        ? cwc.command.scenario_id.value()
        : dsm.getUserSettings().defaultScenario;
    LOG_INFO(State, "SimRun command received, using scenario '{}'", toString(scenarioId));

    // Validate max_frame_ms parameter.
    if (cwc.command.max_frame_ms < 0) {
        LOG_ERROR(State, "Invalid max_frame_ms value: {}", cwc.command.max_frame_ms);
        cwc.sendResponse(
            Api::SimRun::Response::error(
                ApiError("max_frame_ms must be >= 0 (0 = unlimited, >0 = frame rate cap)")));
        return Idle{};
    }

    // Create new SimRunning state with world.
    SimRunning newState;

    // Get scenario metadata first to check for required dimensions.
    auto& registry = dsm.getScenarioRegistry();
    const ScenarioMetadata* metadata = registry.getMetadata(scenarioId);

    if (!metadata) {
        LOG_ERROR(State, "Scenario '{}' not found in registry", toString(scenarioId));
        cwc.sendResponse(
            Api::SimRun::Response::error(
                ApiError(std::string("Scenario not found: ") + std::string(toString(scenarioId)))));
        return Idle{};
    }

    // Determine world dimensions: container-based > scenario requirements > defaults.
    uint32_t worldWidth = dsm.defaultWidth;
    uint32_t worldHeight = dsm.defaultHeight;

    if (cwc.command.container_size.x > 0 && cwc.command.container_size.y > 0) {
        constexpr int targetCellSize = 16;
        worldWidth = static_cast<uint32_t>(cwc.command.container_size.x / targetCellSize);
        worldHeight = static_cast<uint32_t>(cwc.command.container_size.y / targetCellSize);
        worldWidth = std::max(worldWidth, 10u);
        worldHeight = std::max(worldHeight, 10u);
    }
    else if (metadata->requiredWidth > 0 && metadata->requiredHeight > 0) {
        worldWidth = metadata->requiredWidth;
        worldHeight = metadata->requiredHeight;
    }

    // Create world with appropriate dimensions.
    LOG_INFO(
        State,
        "Creating World {}x{} (container: {}x{})",
        worldWidth,
        worldHeight,
        cwc.command.container_size.x,
        cwc.command.container_size.y);
    newState.world = std::make_unique<World>(worldWidth, worldHeight);

    // Create scenario instance from factory.
    newState.scenario = registry.createScenario(scenarioId);
    assert(newState.scenario && "Scenario factory failed after metadata check");

    // Set scenario ID in state.
    newState.scenario_id = scenarioId;

    // Apply config from server settings and user settings.
    const ScenarioConfig scenarioConfig = buildScenarioConfigForRun(dsm, scenarioId);
    newState.scenario->setConfig(scenarioConfig, *newState.world);

    // Run scenario setup to initialize world.
    newState.scenario->setup(*newState.world);

    // Register scenario with World for tick during advanceTime.
    newState.world->setScenario(newState.scenario.get());

    LOG_INFO(State, "Scenario '{}' applied to new world", toString(scenarioId));

    // Set run parameters.
    newState.stepDurationMs = cwc.command.timestep * 1000.0; // Convert seconds to milliseconds.
    newState.targetSteps =
        cwc.command.max_steps > 0 ? static_cast<uint32_t>(cwc.command.max_steps) : 0;
    newState.stepCount = 0;
    newState.frameLimit = cwc.command.max_frame_ms;

    spdlog::info(
        "Idle: World created, transitioning to {} (timestep={}ms, max_steps={}, "
        "max_frame_ms={})",
        cwc.command.start_paused ? "SimPaused" : "SimRunning",
        newState.stepDurationMs,
        cwc.command.max_steps,
        newState.frameLimit);

    // Send response: running=false if starting paused.
    cwc.sendResponse(Api::SimRun::Response::okay({ !cwc.command.start_paused, 0 }));

    // Transition to SimRunning or SimPaused based on start_paused flag.
    if (cwc.command.start_paused) {
        return SimPaused{ std::move(newState) };
    }
    return newState;
}

} // namespace State
} // namespace Server
} // namespace DirtSim
