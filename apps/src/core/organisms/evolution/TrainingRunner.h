#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/DuckClockEvaluationTracker.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/FitnessCalculator.h"
#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/organisms/evolution/OrganismTracker.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/organisms/evolution/TreeEvaluator.h"
#include "core/scenarios/clock_scenario/ClockEventTypes.h"
#include "core/scenarios/nes/NesFitnessDetails.h"
#include "core/scenarios/nes/NesGameAdapter.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"
#include "core/scenarios/nes/NesPaletteFrame.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace DirtSim {

class ClockScenario;
class GenomeRepository;
class NesGameAdapter;
class NesScenarioRuntime;
class NesSmolnesScenarioDriver;
namespace Organism {
class Body;
}
class ScenarioRunner;
class World;

/**
 * Incrementally evaluates a single organism by stepping a World one frame at a time.
 *
 * Unlike blocking evaluation, this allows the caller to:
 * - Process events between steps (cancel, pause).
 * - Access the World for rendering.
 * - Track progress during evaluation.
 */
class TrainingRunner {
public:
    enum class State {
        Running,
        OrganismDied,
        TimeExpired,
    };

    struct Status {
        State state = State::Running;
        double simTime = 0.0;
        double maxEnergy = 0.0;
        double lifespan = 0.0;
        int commandsAccepted = 0;
        int commandsRejected = 0;
        int idleCancels = 0;
        uint64_t nesFramesSurvived = 0;
        double nesRewardTotal = 0.0;
        uint8_t nesControllerMask = 0;
        bool exitedThroughDoor = false;
        double exitDoorTime = 0.0;
    };

    struct BrainSpec {
        std::string brainKind;
        std::optional<std::string> brainVariant;
    };

    struct NesFrameTrace {
        uint64_t advancedFrames = 0;
        uint64_t renderedFramesAfter = 0;
        uint64_t renderedFramesBefore = 0;
        uint8_t inferredControllerMask = 0;
        uint8_t resolvedControllerMask = 0;
        bool evaluationDone = false;
        bool frameAdvanced = false;
        double rewardDelta = 0.0;
        std::optional<NesGameAdapterControllerSource> controllerSource = std::nullopt;
        std::optional<uint64_t> controllerSourceFrameIndex = std::nullopt;
        std::optional<NesGameAdapterDebugState> debugState = std::nullopt;
        std::optional<uint8_t> lastGameStateAfter = std::nullopt;
        std::optional<uint8_t> lastGameStateBefore = std::nullopt;
        std::string commandOutcome;
        std::string commandSignature;
    };

    struct FrameTrace {
        State state = State::Running;
        double simTime = 0.0;
        uint64_t stepOrdinal = 0;
        std::optional<NesFrameTrace> nes = std::nullopt;
    };

    using FrameTraceSink = std::function<void(const FrameTrace&)>;

    struct Individual {
        BrainSpec brain;
        Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
        std::optional<Genome> genome;
    };

    struct Config {
        TrainingBrainRegistry brainRegistry;
        NesGameAdapterRegistry nesGameAdapterRegistry = NesGameAdapterRegistry::createDefault();
        std::optional<bool> duckClockSpawnLeftFirst = std::nullopt;
        std::optional<uint32_t> duckClockSpawnRngSeed = std::nullopt;
        FrameTraceSink frameTraceSink = nullptr;
        bool nesApuEnabled = false;
        bool nesDetailedTimingEnabled = false;
        bool nesRgbaOutputEnabled = true;
        std::optional<ScenarioConfig> scenarioConfigOverride = std::nullopt;
    };

    TrainingRunner(
        const TrainingSpec& trainingSpec,
        const Individual& individual,
        const EvolutionConfig& evolutionConfig,
        GenomeRepository& genomeRepository);

    TrainingRunner(
        const TrainingSpec& trainingSpec,
        const Individual& individual,
        const EvolutionConfig& evolutionConfig,
        GenomeRepository& genomeRepository,
        const Config& runnerConfig);
    ~TrainingRunner();

    TrainingRunner(const TrainingRunner&) = delete;
    TrainingRunner& operator=(const TrainingRunner&) = delete;

    TrainingRunner(TrainingRunner&&) noexcept;
    TrainingRunner& operator=(TrainingRunner&&) noexcept;

    Status step(int frames = 1);
    Status getStatus() const;

    const World* getWorld() const { return world_.get(); }
    World* getWorld() { return world_.get(); }
    bool isNesScenario() const { return nesDriver_ != nullptr; }
    const std::optional<ScenarioVideoFrame>& getScenarioVideoFrame() const
    {
        return nesScenarioVideoFrame_;
    }
    const WorldData* getWorldData() const;
    const std::vector<OrganismId>* getOrganismGrid() const;
    const Timers* getTimers() const;
    ScenarioConfig getScenarioConfig() const;
    Result<std::monostate, std::string> setScenarioConfig(const ScenarioConfig& config);

    std::optional<DuckEvaluationArtifacts> getDuckEvaluationArtifacts() const;
    const Organism::Body* getOrganism() const;
    const OrganismTrackingHistory& getOrganismTrackingHistory() const;
    const std::optional<TreeResourceTotals>& getTreeResourceTotals() const;
    const NesFitnessDetails& getNesFitnessDetails() const;
    const std::optional<NesGameAdapterControllerOutput>& getNesLastControllerOutput() const
    {
        return nesLastControllerOutput_;
    }
    const std::optional<NesGameAdapterDebugState>& getNesLastDebugState() const
    {
        return nesLastDebugState_;
    }
    const std::optional<NesControllerTelemetry>& getNesLastControllerTelemetry() const
    {
        return nesLastControllerTelemetry_;
    }
    std::vector<std::pair<std::string, int>> getTopCommandSignatures(size_t maxEntries) const;
    std::vector<std::pair<std::string, int>> getTopCommandOutcomeSignatures(
        size_t maxEntries) const;

    double getSimTime() const { return simTime_; }
    double getMaxTime() const { return maxTime_; }
    float getProgress() const { return static_cast<float>(simTime_ / maxTime_); }

    double getCurrentMaxEnergy() const;
    bool isOrganismAlive() const;

    static constexpr double TIMESTEP = 0.016;

private:
    void resolveBrainEntry();
    NesFrameTrace runScenarioDrivenStep();
    DuckSensoryData makeNesDuckSensoryData() const;
    uint8_t inferNesControllerMask();
    std::optional<DuckEvaluationArtifacts> buildDuckEvaluationArtifacts(const Duck& duck) const;
    void snapshotDuckEvaluationArtifacts();
    void spawnEvaluationOrganism();
    void initDuckClockDoors();
    void updateDuckClockEvaluationTracker(const Duck& duck);
    void updateDuckClockDoors();
    static Config makeDefaultConfig();
    ScenarioConfig buildEffectiveScenarioConfig(const ScenarioConfig& config) const;

    TrainingSpec trainingSpec_;
    Individual individual_;
    std::unique_ptr<World> world_;
    std::unique_ptr<ScenarioRunner> scenario_;
    std::unique_ptr<NesSmolnesScenarioDriver> nesDriver_;
    ScenarioConfig nesScenarioConfig_;
    WorldData nesWorldData_;
    Timers nesTimers_;
    std::optional<ScenarioVideoFrame> nesScenarioVideoFrame_;
    OrganismId organismId_ = INVALID_ORGANISM_ID;

    double simTime_ = 0.0;
    double maxTime_ = 600.0;
    OrganismTracker organismTracker_;
    TreeEvaluator treeEvaluator_;

    State state_ = State::Running;
    TrainingBrainRegistry brainRegistry_;
    NesGameAdapterRegistry nesGameAdapterRegistry_;
    std::optional<bool> duckClockSpawnLeftFirst_ = std::nullopt;
    std::mt19937 spawnRng_;
    EvolutionConfig evolutionConfig_;
    FrameTraceSink frameTraceSink_ = nullptr;
    uint64_t stepOrdinal_ = 0;
    BrainRegistryEntry::ControlMode controlMode_ = BrainRegistryEntry::ControlMode::OrganismDriven;
    NesScenarioRuntime* nesRuntime_ = nullptr;
    bool nesApuEnabled_ = false;
    bool nesDetailedTimingEnabled_ = false;
    bool nesRgbaOutputEnabled_ = false;
    std::unique_ptr<NesGameAdapter> nesGameAdapter_;
    uint8_t nesControllerMask_ = 0;
    std::optional<NesPaletteFrame> nesPaletteFrame_ = std::nullopt;
    std::unique_ptr<DuckNeuralNetRecurrentBrainV2> nesDuckBrainV2_;
    std::optional<NesGameAdapterControllerOutput> nesLastControllerOutput_ = std::nullopt;
    std::optional<NesControllerTelemetry> nesLastControllerTelemetry_ = std::nullopt;
    std::optional<NesGameAdapterDebugState> nesLastDebugState_ = std::nullopt;
    std::optional<uint8_t> nesLastGameState_ = std::nullopt;
    std::unordered_map<std::string, int> nesCommandOutcomeSignatureCounts_;
    std::unordered_map<std::string, int> nesCommandSignatureCounts_;
    NesFitnessDetails nesFitnessDetails_{};
    uint64_t nesFramesSurvived_ = 0;
    double nesRewardTotal_ = 0.0;

    struct DuckClockDoorState {
        ClockScenario* clockScenario = nullptr;
        DoorId entranceDoorId = INVALID_DOOR_ID;
        DoorId exitDoorId = INVALID_DOOR_ID;
        DoorSide side = DoorSide::LEFT;
        bool duckReachedOppositeWall = false;
        bool entranceDoorClosed = false;
        bool exitDoorOpened = false;
        bool duckExitedThroughDoor = false;
        double exitDoorOpenTime = 0.0;
        double exitDoorTime = 0.0;
    };
    std::optional<DuckClockDoorState> duckClockDoors_;
    std::optional<DuckClockEvaluationTracker> duckClockEvaluationTracker_;
    std::optional<DuckEvaluationArtifacts> duckEvaluationArtifacts_;

    struct ChildSeedState {
        OrganismId id = INVALID_ORGANISM_ID;
        Vector2i spawnPosition;
        Vector2i lastPosition;
        double timeAtCurrentPosition = 0.0;
        bool landed = false;
    };
    std::vector<ChildSeedState> childSeeds_;
};

} // namespace DirtSim
