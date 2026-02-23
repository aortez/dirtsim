#include "TrainingRunner.h"
#include "EvolutionConfig.h"
#include "FitnessCalculator.h"
#include "GenomeRepository.h"
#include "core/Assert.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/evolution/NesDuckSpecialSenseLayout.h"
#include "core/scenarios/NesFlappyParatroopaScenario.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace DirtSim {

namespace {
constexpr uint8_t kNesStateTitle = 0;
constexpr uint8_t kNesStateWaiting = 1;
constexpr uint8_t kNesStateGameOver = 7;
constexpr uint8_t kNesStateFadeIn = 8;
constexpr uint8_t kNesStateTitleFade = 9;
constexpr uint32_t kNesStartPulsePeriodFrames = 12;
constexpr uint32_t kNesStartPulseWidthFrames = 2;
constexpr uint32_t kNesWaitingFlapPulsePeriodFrames = 8;
constexpr uint32_t kNesWaitingFlapPulseWidthFrames = 1;
constexpr float kNesDuckMoveThreshold = 0.2f;
constexpr float kNesDuckVelocityScale = 10.0f;
constexpr uint8_t kNesHistogramMask = NesPolicyLayout::ButtonA | NesPolicyLayout::ButtonLeft
    | NesPolicyLayout::ButtonRight | NesPolicyLayout::ButtonStart;
static_assert(
    NesPolicyLayout::InputCount == NesDuckSpecialSenseLayout::FlappyMappedCount,
    "TrainingRunner: Flappy feature count must match special-sense mapping");

void addSignatureCount(std::unordered_map<std::string, int>& counts, const std::string& signature)
{
    auto it = counts.find(signature);
    if (it == counts.end()) {
        counts.emplace(signature, 1);
        return;
    }
    ++it->second;
}

std::vector<std::pair<std::string, int>> getTopSignatureEntries(
    const std::unordered_map<std::string, int>& counts, size_t maxEntries)
{
    if (maxEntries == 0 || counts.empty()) {
        return {};
    }

    std::vector<std::pair<std::string, int>> entries;
    entries.reserve(counts.size());
    for (const auto& [signature, count] : counts) {
        entries.emplace_back(signature, count);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) {
            return lhs.second > rhs.second;
        }
        return lhs.first < rhs.first;
    });

    if (entries.size() > maxEntries) {
        entries.resize(maxEntries);
    }

    return entries;
}

std::string buildNesCommandSignature(uint8_t controllerMask)
{
    controllerMask &= kNesHistogramMask;
    if (controllerMask == 0u) {
        return "Idle";
    }

    std::string signature;
    const auto appendToken = [&signature](const char* token) {
        if (!signature.empty()) {
            signature += "+";
        }
        signature += token;
    };

    if ((controllerMask & NesPolicyLayout::ButtonStart) != 0u) {
        appendToken("Start");
    }
    if ((controllerMask & NesPolicyLayout::ButtonA) != 0u) {
        appendToken("Flap");
    }
    if ((controllerMask & NesPolicyLayout::ButtonLeft) != 0u) {
        appendToken("Left");
    }
    if ((controllerMask & NesPolicyLayout::ButtonRight) != 0u) {
        appendToken("Right");
    }

    return signature;
}

bool isTitleLikeNesState(uint8_t gameState)
{
    return gameState == kNesStateTitle || gameState == kNesStateGameOver
        || gameState == kNesStateFadeIn || gameState == kNesStateTitleFade;
}

void mapFlappyParatroopaFeaturesToSpecialSenses(
    const std::array<float, NesPolicyLayout::InputCount>& features,
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT>& specialSenses)
{
    const auto writeSense =
        [&features, &specialSenses](NesDuckSpecialSenseLayout::Slot slot, int featureIndex) {
            specialSenses[static_cast<size_t>(slot)] =
                static_cast<double>(features[static_cast<size_t>(featureIndex)]);
        };

    writeSense(NesDuckSpecialSenseLayout::Bias, NesDuckSpecialSenseLayout::Bias);
    writeSense(
        NesDuckSpecialSenseLayout::BirdYNormalized, NesDuckSpecialSenseLayout::BirdYNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::BirdVelocityNormalized,
        NesDuckSpecialSenseLayout::BirdVelocityNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::NextPipeDistanceNormalized,
        NesDuckSpecialSenseLayout::NextPipeDistanceNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::NextPipeTopNormalized,
        NesDuckSpecialSenseLayout::NextPipeTopNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::NextPipeBottomNormalized,
        NesDuckSpecialSenseLayout::NextPipeBottomNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::BirdGapOffsetNormalized,
        NesDuckSpecialSenseLayout::BirdGapOffsetNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::ScrollXNormalized, NesDuckSpecialSenseLayout::ScrollXNormalized);
    writeSense(NesDuckSpecialSenseLayout::ScrollNt, NesDuckSpecialSenseLayout::ScrollNt);
    writeSense(
        NesDuckSpecialSenseLayout::GameStateNormalized,
        NesDuckSpecialSenseLayout::GameStateNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::ScoreNormalized, NesDuckSpecialSenseLayout::ScoreNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::PrevFlapPressed, NesDuckSpecialSenseLayout::PrevFlapPressed);
}

void mapNesFeaturesToDuckSpecialSenses(
    const std::string& romId,
    const std::array<float, NesPolicyLayout::InputCount>& features,
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT>& specialSenses)
{
    specialSenses.fill(0.0);
    if (romId == NesPolicyLayout::FlappyParatroopaWorldUnlRomId) {
        mapFlappyParatroopaFeaturesToSpecialSenses(features, specialSenses);
    }
}

Vector2i findSpawnCell(World& world)
{
    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;
    const int centerX = width / 2;
    const int centerY = height / 2;

    const auto isSpawnable = [&world, &data](int x, int y) {
        if (!data.inBounds(x, y)) {
            return false;
        }
        if (!data.at(x, y).isAir()) {
            return false;
        }
        return !world.getOrganismManager().hasOrganism({ x, y });
    };

    if (isSpawnable(centerX, centerY)) {
        return { centerX, centerY };
    }

    auto findNearestInRows = [&](int startY, int endY) -> std::optional<Vector2i> {
        if (startY > endY) {
            return std::nullopt;
        }

        long long bestDistance = std::numeric_limits<long long>::max();
        Vector2i best{ 0, 0 };
        bool found = false;

        for (int y = startY; y <= endY; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!isSpawnable(x, y)) {
                    continue;
                }
                const long long dx = static_cast<long long>(x) - centerX;
                const long long dy = static_cast<long long>(y) - centerY;
                const long long distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = { x, y };
                    found = true;
                }
            }
        }

        if (!found) {
            return std::nullopt;
        }
        return best;
    };

    if (auto above = findNearestInRows(0, centerY); above.has_value()) {
        return above.value();
    }

    if (auto below = findNearestInRows(centerY + 1, height - 1); below.has_value()) {
        return below.value();
    }

    if (world.getOrganismManager().hasOrganism({ centerX, centerY })) {
        DIRTSIM_ASSERT(false, "TrainingRunner: Spawn location already occupied");
    }

    data.at(centerX, centerY).clear();
    return { centerX, centerY };
}
} // namespace

TrainingRunner::TrainingRunner(
    const TrainingSpec& trainingSpec,
    const Individual& individual,
    const EvolutionConfig& evolutionConfig,
    GenomeRepository& genomeRepository)
    : TrainingRunner(
          trainingSpec, individual, evolutionConfig, genomeRepository, makeDefaultConfig())
{}

TrainingRunner::TrainingRunner(
    const TrainingSpec& trainingSpec,
    const Individual& individual,
    const EvolutionConfig& evolutionConfig,
    GenomeRepository& genomeRepository,
    const Config& runnerConfig)
    : trainingSpec_(trainingSpec),
      individual_(individual),
      maxTime_(evolutionConfig.maxSimulationTime),
      brainRegistry_(runnerConfig.brainRegistry),
      duckClockSpawnLeftFirst_(runnerConfig.duckClockSpawnLeftFirst),
      spawnRng_(runnerConfig.duckClockSpawnRngSeed.value_or(
          static_cast<uint32_t>(std::random_device{}()))),
      evolutionConfig_(evolutionConfig)
{
    resolveBrainEntry();

    // Create scenario from registry.
    auto registry = ScenarioRegistry::createDefault(genomeRepository);
    scenario_ = registry.createScenario(individual_.scenarioId);
    DIRTSIM_ASSERT(scenario_, "TrainingRunner: Scenario factory returned null");

    // Create world with scenario's required dimensions.
    const auto& metadata = scenario_->getMetadata();
    int width = metadata.requiredWidth > 0 ? metadata.requiredWidth : 9;
    int height = metadata.requiredHeight > 0 ? metadata.requiredHeight : 9;
    world_ = std::make_unique<World>(width, height);

    if (trainingSpec_.organismType == OrganismType::DUCK
        && individual_.scenarioId == Scenario::EnumType::Clock) {
        ScenarioConfig scenarioConfig = scenario_->getConfig();
        if (auto* clockConfig = std::get_if<DirtSim::Config::Clock>(&scenarioConfig)) {
            clockConfig->duckEnabled = false;
            clockConfig->meltdownEnabled = false;
            clockConfig->rainEnabled = false;
            scenario_->setConfig(scenarioConfig, *world_);
        }
    }

    // Setup scenario.
    scenario_->setup(*world_);
    world_->setScenario(scenario_.get());

    nesPolicyInputs_.fill(0.0f);
    if (controlMode_ == BrainRegistryEntry::ControlMode::ScenarioDriven
        && individual_.brain.brainKind == TrainingBrainKind::DuckNeuralNetRecurrent) {
        DIRTSIM_ASSERT(
            individual_.genome.has_value(),
            "TrainingRunner: NES duck recurrent controller requires a genome");
        nesDuckBrain_ = std::make_unique<DuckNeuralNetRecurrentBrain>(individual_.genome.value());
    }

    nesRuntimeRomId_.clear();
    if (auto* nesScenario = dynamic_cast<NesFlappyParatroopaScenario*>(scenario_.get())) {
        nesRuntimeRomId_ = nesScenario->getRuntimeResolvedRomId();
        nesRomExtractor_.emplace(nesRuntimeRomId_);
        nesFlappyEvaluator_.emplace();
        nesFlappyEvaluator_->reset();
    }
}

TrainingRunner::~TrainingRunner() = default;

TrainingRunner::TrainingRunner(TrainingRunner&&) noexcept = default;
TrainingRunner& TrainingRunner::operator=(TrainingRunner&&) noexcept = default;

TrainingRunner::Config TrainingRunner::makeDefaultConfig()
{
    Config config{};
    config.brainRegistry = TrainingBrainRegistry::createDefault();
    return config;
}

TrainingRunner::Status TrainingRunner::step(int frames)
{
    if (state_ != State::Running) {
        return getStatus();
    }

    if (controlMode_ == BrainRegistryEntry::ControlMode::OrganismDriven
        && organismId_ == INVALID_ORGANISM_ID) {
        spawnEvaluationOrganism();
    }

    for (int i = 0; i < frames && state_ == State::Running; ++i) {
        if (controlMode_ == BrainRegistryEntry::ControlMode::ScenarioDriven) {
            runScenarioDrivenStep();
            if (simTime_ >= maxTime_ && state_ == State::Running) {
                state_ = State::TimeExpired;
            }
            continue;
        }

        world_->advanceTime(TIMESTEP);
        simTime_ += TIMESTEP;

        Organism::Body* organism = world_->getOrganismManager().getOrganism(organismId_);
        if (!organism) {
            state_ = State::OrganismDied;
            break;
        }
        organismTracker_.track(simTime_, organism->position);

        if (trainingSpec_.organismType == OrganismType::TREE) {
            Tree* tree = world_->getOrganismManager().getTree(organismId_);
            if (tree) {
                treeEvaluator_.update(*tree);
                const FitnessResult result{
                    .lifespan = tree->getAge(),
                    .maxEnergy = treeEvaluator_.getMaxEnergy(),
                    .commandsAccepted = treeEvaluator_.getCommandAcceptedCount(),
                    .commandsRejected = treeEvaluator_.getCommandRejectedCount(),
                    .idleCancels = treeEvaluator_.getIdleCancelCount(),
                };
                const WorldData& data = world_->getData();
                const FitnessContext context{
                    .result = result,
                    .organismType = OrganismType::TREE,
                    .worldWidth = data.width,
                    .worldHeight = data.height,
                    .evolutionConfig = evolutionConfig_,
                    .finalOrganism = tree,
                    .organismTrackingHistory = &organismTracker_.getHistory(),
                    .treeResources = &tree->getResourceTotals(),
                };
                TreeEvaluator::evaluate(context);
            }
        }

        // Check time limit.
        if (simTime_ >= maxTime_) {
            state_ = State::TimeExpired;
            break;
        }
    }

    return getStatus();
}

TrainingRunner::Status TrainingRunner::getStatus() const
{
    Status status;
    status.state = state_;
    status.simTime = simTime_;
    status.maxEnergy = treeEvaluator_.getMaxEnergy();
    status.commandsAccepted = treeEvaluator_.getCommandAcceptedCount();
    status.commandsRejected = treeEvaluator_.getCommandRejectedCount();
    status.idleCancels = treeEvaluator_.getIdleCancelCount();
    status.nesFramesSurvived = nesFramesSurvived_;
    status.nesRewardTotal = nesRewardTotal_;
    status.nesControllerMask = nesControllerMask_;

    if (const Organism::Body* organism = getOrganism()) {
        status.lifespan = organism->getAge();
    }
    else {
        status.lifespan = simTime_;
    }

    return status;
}

const OrganismTrackingHistory& TrainingRunner::getOrganismTrackingHistory() const
{
    return organismTracker_.getHistory();
}

const std::optional<TreeResourceTotals>& TrainingRunner::getTreeResourceTotals() const
{
    return treeEvaluator_.getResourceTotals();
}

std::vector<std::pair<std::string, int>> TrainingRunner::getTopCommandSignatures(
    size_t maxEntries) const
{
    const Organism::Body* organism = getOrganism();
    if (organism) {
        return organism->getTopCommandSignatures(maxEntries);
    }

    if (controlMode_ == BrainRegistryEntry::ControlMode::ScenarioDriven) {
        return getTopSignatureEntries(nesCommandSignatureCounts_, maxEntries);
    }

    return {};
}

std::vector<std::pair<std::string, int>> TrainingRunner::getTopCommandOutcomeSignatures(
    size_t maxEntries) const
{
    const Organism::Body* organism = getOrganism();
    if (organism) {
        return organism->getTopCommandOutcomeSignatures(maxEntries);
    }

    if (controlMode_ == BrainRegistryEntry::ControlMode::ScenarioDriven) {
        return getTopSignatureEntries(nesCommandOutcomeSignatureCounts_, maxEntries);
    }

    return {};
}

double TrainingRunner::getCurrentMaxEnergy() const
{
    return treeEvaluator_.getMaxEnergy();
}

const Organism::Body* TrainingRunner::getOrganism() const
{
    if (!world_) {
        return nullptr;
    }
    return world_->getOrganismManager().getOrganism(organismId_);
}

ScenarioConfig TrainingRunner::getScenarioConfig() const
{
    if (!scenario_) {
        return DirtSim::Config::Empty{};
    }
    return scenario_->getConfig();
}

bool TrainingRunner::isOrganismAlive() const
{
    if (controlMode_ == BrainRegistryEntry::ControlMode::ScenarioDriven) {
        return state_ == State::Running;
    }
    return getOrganism() != nullptr;
}

void TrainingRunner::resolveBrainEntry()
{
    const std::string variant = individual_.brain.brainVariant.value_or("");
    const BrainRegistryEntry* entry =
        brainRegistry_.find(trainingSpec_.organismType, individual_.brain.brainKind, variant);
    DIRTSIM_ASSERT(entry != nullptr, "TrainingRunner: Brain kind is not registered");

    const Genome* genomePtr =
        individual_.genome.has_value() ? &individual_.genome.value() : nullptr;
    if (entry->requiresGenome) {
        DIRTSIM_ASSERT(genomePtr != nullptr, "TrainingRunner: Genome required but missing");
    }

    controlMode_ = entry->controlMode;
}

void TrainingRunner::runScenarioDrivenStep()
{
    DIRTSIM_ASSERT(world_, "TrainingRunner: World must exist before stepping");
    DIRTSIM_ASSERT(scenario_, "TrainingRunner: Scenario must exist before stepping");

    auto* nesScenario = dynamic_cast<NesFlappyParatroopaScenario*>(scenario_.get());
    if (!nesScenario) {
        state_ = State::OrganismDied;
        return;
    }

    if (!nesScenario->isRuntimeRunning() || !nesScenario->isRuntimeHealthy()) {
        state_ = State::OrganismDied;
        return;
    }

    bool frameAdvanced = false;
    std::string commandOutcome = "NoFrameAdvance";
    const uint64_t renderedFramesBefore = nesScenario->getRuntimeRenderedFrameCount();
    uint8_t controllerMask = inferNesControllerMask();
    const uint8_t gameState = nesLastGameState_.value_or(kNesStateTitle);
    if (isTitleLikeNesState(gameState)) {
        const bool pressStart =
            (nesStartPulseFrameCounter_ % kNesStartPulsePeriodFrames) < kNesStartPulseWidthFrames;
        controllerMask = pressStart ? NesPolicyLayout::ButtonStart : 0u;
        ++nesStartPulseFrameCounter_;
        nesWaitingFlapPulseFrameCounter_ = 0;
    }
    else {
        nesStartPulseFrameCounter_ = 0;
        if (gameState == kNesStateWaiting) {
            const bool pressFlap =
                (nesWaitingFlapPulseFrameCounter_ % kNesWaitingFlapPulsePeriodFrames)
                < kNesWaitingFlapPulseWidthFrames;
            controllerMask = pressFlap ? NesPolicyLayout::ButtonA : 0u;
            ++nesWaitingFlapPulseFrameCounter_;
        }
        else {
            nesWaitingFlapPulseFrameCounter_ = 0;
        }
    }

    const std::string commandSignature = buildNesCommandSignature(controllerMask);
    addSignatureCount(nesCommandSignatureCounts_, commandSignature);

    nesControllerMask_ = controllerMask;
    nesScenario->setController1State(nesControllerMask_);

    world_->advanceTime(TIMESTEP);
    simTime_ += TIMESTEP;

    const uint64_t renderedFramesAfter = nesScenario->getRuntimeRenderedFrameCount();
    if (renderedFramesAfter > renderedFramesBefore) {
        frameAdvanced = true;
        commandOutcome = "FrameAdvanced";
        const uint64_t advancedFrames = renderedFramesAfter - renderedFramesBefore;
        nesFramesSurvived_ += advancedFrames;

        if (nesRomExtractor_.has_value() && nesRomExtractor_->isSupported()
            && nesFlappyEvaluator_.has_value()) {
            const auto snapshot = nesScenario->copyRuntimeMemorySnapshot();
            if (snapshot.has_value()) {
                const std::optional<NesFlappyBirdEvaluatorInput> evaluatorInput =
                    nesRomExtractor_->extract(snapshot.value(), nesControllerMask_);
                if (evaluatorInput.has_value()) {
                    const NesFlappyBirdEvaluatorOutput evaluation =
                        nesFlappyEvaluator_->evaluate(evaluatorInput.value());
                    nesPolicyInputs_ = evaluation.features;
                    nesLastGameState_ = evaluation.gameState;
                    nesRewardTotal_ += evaluation.rewardDelta;
                    if (evaluation.done) {
                        state_ = State::OrganismDied;
                        commandOutcome = "EpisodeEnd";
                    }
                }
            }
        }
        else {
            nesRewardTotal_ += static_cast<double>(advancedFrames);
        }
    }

    if (state_ == State::Running
        && (!nesScenario->isRuntimeRunning() || !nesScenario->isRuntimeHealthy())) {
        state_ = State::OrganismDied;
        commandOutcome = "EpisodeEnd";
    }
    if (state_ != State::Running && frameAdvanced) {
        commandOutcome = "EpisodeEnd";
    }

    addSignatureCount(
        nesCommandOutcomeSignatureCounts_, commandSignature + " -> " + commandOutcome);
}

DuckSensoryData TrainingRunner::makeNesDuckSensoryData() const
{
    DuckSensoryData sensory{};
    sensory.actual_width = DuckSensoryData::GRID_SIZE;
    sensory.actual_height = DuckSensoryData::GRID_SIZE;
    sensory.scale_factor = 1.0;
    sensory.world_offset = { 0, 0 };
    sensory.position = { DuckSensoryData::GRID_SIZE / 2, DuckSensoryData::GRID_SIZE / 2 };
    sensory.delta_time_seconds = TIMESTEP;

    const auto setDominantMaterial = [&sensory](int x, int y, Material::EnumType materialType) {
        if (x < 0 || x >= DuckSensoryData::GRID_SIZE || y < 0 || y >= DuckSensoryData::GRID_SIZE) {
            return;
        }
        auto& histogram =
            sensory.material_histograms[static_cast<size_t>(y)][static_cast<size_t>(x)];
        histogram.fill(0.0);
        const size_t materialIndex = static_cast<size_t>(materialType);
        DIRTSIM_ASSERT(
            materialIndex < histogram.size(),
            "TrainingRunner: Material index out of range for duck sensory histogram");
        histogram[materialIndex] = 1.0;
    };

    for (int y = 0; y < DuckSensoryData::GRID_SIZE; ++y) {
        for (int x = 0; x < DuckSensoryData::GRID_SIZE; ++x) {
            setDominantMaterial(x, y, Material::EnumType::Air);
        }
    }

    const int birdX = 3;
    const float birdYNormalized = std::clamp(
        nesPolicyInputs_[static_cast<size_t>(NesDuckSpecialSenseLayout::BirdYNormalized)],
        0.0f,
        1.0f);
    const int birdY = std::clamp(
        static_cast<int>(
            std::lround(birdYNormalized * static_cast<float>(DuckSensoryData::GRID_SIZE - 1))),
        0,
        DuckSensoryData::GRID_SIZE - 1);
    setDominantMaterial(birdX, birdY, Material::EnumType::Wood);

    const float pipeDistanceNormalized = std::clamp(
        nesPolicyInputs_[static_cast<size_t>(
            NesDuckSpecialSenseLayout::NextPipeDistanceNormalized)],
        0.0f,
        1.0f);
    const float pipeTopNormalized = std::clamp(
        nesPolicyInputs_[static_cast<size_t>(NesDuckSpecialSenseLayout::NextPipeTopNormalized)],
        0.0f,
        1.0f);
    const float pipeBottomNormalized = std::clamp(
        nesPolicyInputs_[static_cast<size_t>(NesDuckSpecialSenseLayout::NextPipeBottomNormalized)],
        0.0f,
        1.0f);
    const int pipeX = std::clamp(
        birdX
            + static_cast<int>(std::lround(
                pipeDistanceNormalized
                * static_cast<float>(DuckSensoryData::GRID_SIZE - 1 - birdX))),
        birdX + 1,
        DuckSensoryData::GRID_SIZE - 1);
    const int gapTop = std::clamp(
        static_cast<int>(
            std::lround(pipeTopNormalized * static_cast<float>(DuckSensoryData::GRID_SIZE - 1))),
        0,
        DuckSensoryData::GRID_SIZE - 1);
    const int gapBottom = std::clamp(
        static_cast<int>(
            std::lround(pipeBottomNormalized * static_cast<float>(DuckSensoryData::GRID_SIZE - 1))),
        gapTop,
        DuckSensoryData::GRID_SIZE - 1);
    for (int pipeColumn = pipeX; pipeColumn <= std::min(pipeX + 1, DuckSensoryData::GRID_SIZE - 1);
         ++pipeColumn) {
        for (int y = 0; y < DuckSensoryData::GRID_SIZE; ++y) {
            if (y >= gapTop && y <= gapBottom) {
                continue;
            }
            setDominantMaterial(pipeColumn, y, Material::EnumType::Wall);
        }
    }

    const float birdVelocityNormalized = std::clamp(
        nesPolicyInputs_[static_cast<size_t>(NesDuckSpecialSenseLayout::BirdVelocityNormalized)],
        -1.0f,
        1.0f);
    sensory.velocity.x = 0.0;
    sensory.velocity.y = static_cast<double>(birdVelocityNormalized * kNesDuckVelocityScale);
    mapNesFeaturesToDuckSpecialSenses(nesRuntimeRomId_, nesPolicyInputs_, sensory.special_senses);

    if ((nesControllerMask_ & NesPolicyLayout::ButtonLeft) != 0u) {
        sensory.facing_x = -1.0f;
        sensory.velocity.x = -1.0;
    }
    else if ((nesControllerMask_ & NesPolicyLayout::ButtonRight) != 0u) {
        sensory.facing_x = 1.0f;
        sensory.velocity.x = 1.0;
    }
    else {
        sensory.facing_x = 1.0f;
    }

    sensory.on_ground =
        nesLastGameState_.has_value() && nesLastGameState_.value() == kNesStateWaiting;
    return sensory;
}

uint8_t TrainingRunner::inferNesControllerMask()
{
    if (individual_.brain.brainKind != TrainingBrainKind::DuckNeuralNetRecurrent
        || !nesDuckBrain_) {
        return 0;
    }

    const DuckSensoryData sensory = makeNesDuckSensoryData();
    const DuckInput input = nesDuckBrain_->inferInput(sensory);

    uint8_t mask = 0;
    if (input.jump) {
        mask |= NesPolicyLayout::ButtonA;
    }
    if (input.move.x <= -kNesDuckMoveThreshold) {
        mask |= NesPolicyLayout::ButtonLeft;
    }
    else if (input.move.x >= kNesDuckMoveThreshold) {
        mask |= NesPolicyLayout::ButtonRight;
    }

    return mask;
}

void TrainingRunner::spawnEvaluationOrganism()
{
    DIRTSIM_ASSERT(
        controlMode_ == BrainRegistryEntry::ControlMode::OrganismDriven,
        "TrainingRunner: Scenario-driven brains do not spawn organisms");
    DIRTSIM_ASSERT(world_, "TrainingRunner: World must exist before spawn");
    DIRTSIM_ASSERT(scenario_, "TrainingRunner: Scenario must exist before spawn");

    Vector2i spawnCell{ 0, 0 };
    const WorldData& data = world_->getData();
    if (trainingSpec_.organismType == OrganismType::DUCK
        && individual_.scenarioId == Scenario::EnumType::Clock) {
        const int spawnY = std::max(1, data.height - 2);
        const int leftX = 1;
        const int rightX = std::max(0, data.width - 2);
        std::array<Vector2i, 2> sideCandidates{
            Vector2i{ leftX, spawnY },
            Vector2i{ rightX, spawnY },
        };

        bool spawnLeftFirst = false;
        if (duckClockSpawnLeftFirst_.has_value()) {
            spawnLeftFirst = duckClockSpawnLeftFirst_.value();
        }
        else {
            std::bernoulli_distribution sideDist(0.5);
            spawnLeftFirst = sideDist(spawnRng_);
        }
        if (!spawnLeftFirst) {
            std::swap(sideCandidates[0], sideCandidates[1]);
        }

        const auto isSpawnable = [&data, this](const Vector2i& cell) {
            return data.inBounds(cell.x, cell.y) && data.at(cell.x, cell.y).isAir()
                && !world_->getOrganismManager().hasOrganism(cell);
        };

        if (isSpawnable(sideCandidates[0])) {
            spawnCell = sideCandidates[0];
        }
        else if (isSpawnable(sideCandidates[1])) {
            spawnCell = sideCandidates[1];
        }
        else {
            spawnCell = findSpawnCell(*world_);
        }
    }
    else {
        spawnCell = findSpawnCell(*world_);
    }

    const std::string variant = individual_.brain.brainVariant.value_or("");
    const BrainRegistryEntry* entry =
        brainRegistry_.find(trainingSpec_.organismType, individual_.brain.brainKind, variant);
    DIRTSIM_ASSERT(entry != nullptr, "TrainingRunner: Brain kind is not registered");

    const Genome* genomePtr =
        individual_.genome.has_value() ? &individual_.genome.value() : nullptr;
    if (entry->requiresGenome) {
        DIRTSIM_ASSERT(genomePtr != nullptr, "TrainingRunner: Genome required but missing");
    }

    organismId_ = entry->spawn(*world_, spawnCell.x, spawnCell.y, genomePtr);
    DIRTSIM_ASSERT(organismId_ != INVALID_ORGANISM_ID, "TrainingRunner: Spawn failed");

    const Organism::Body* organism = world_->getOrganismManager().getOrganism(organismId_);
    DIRTSIM_ASSERT(organism != nullptr, "TrainingRunner: Spawned organism not found");
    organismTracker_.reset();
    organismTracker_.track(simTime_, organism->position);
    if (trainingSpec_.organismType == OrganismType::TREE) {
        treeEvaluator_.start();
    }
}

} // namespace DirtSim
