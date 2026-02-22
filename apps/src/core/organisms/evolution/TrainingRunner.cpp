#include "TrainingRunner.h"
#include "EvolutionConfig.h"
#include "FitnessCalculator.h"
#include "GenomeRepository.h"
#include "core/Assert.h"
#include "core/RenderMessage.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/scenarios/NesScenario.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace DirtSim {

namespace {
float decodeRgb565Luma(std::byte lowByte, std::byte highByte)
{
    const uint16_t packed = static_cast<uint16_t>(std::to_integer<uint8_t>(lowByte))
        | (static_cast<uint16_t>(std::to_integer<uint8_t>(highByte)) << 8);
    const float red = static_cast<float>((packed >> 11) & 0x1Fu) / 31.0f;
    const float green = static_cast<float>((packed >> 5) & 0x3Fu) / 63.0f;
    const float blue = static_cast<float>(packed & 0x1Fu) / 31.0f;
    return (0.299f * red) + (0.587f * green) + (0.114f * blue);
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

    applyNesBrainDefaults();

    // Setup scenario.
    scenario_->setup(*world_);
    world_->setScenario(scenario_.get());
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
    if (!organism) {
        return {};
    }

    return organism->getTopCommandSignatures(maxEntries);
}

std::vector<std::pair<std::string, int>> TrainingRunner::getTopCommandOutcomeSignatures(
    size_t maxEntries) const
{
    const Organism::Body* organism = getOrganism();
    if (!organism) {
        return {};
    }

    return organism->getTopCommandOutcomeSignatures(maxEntries);
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

void TrainingRunner::applyNesBrainDefaults()
{
    if (!scenario_ || !world_) {
        return;
    }

    if (individual_.scenarioId != Scenario::EnumType::Nes) {
        return;
    }

    const std::optional<TrainingBrainDefaults> defaults =
        getTrainingBrainDefaults(individual_.brain.brainKind);
    if (!defaults.has_value() || !defaults->defaultNesRomId.has_value()) {
        return;
    }

    ScenarioConfig scenarioConfig = scenario_->getConfig();
    auto* nesConfig = std::get_if<DirtSim::Config::Nes>(&scenarioConfig);
    if (!nesConfig || !nesConfig->romId.empty()) {
        return;
    }

    nesConfig->romId = defaults->defaultNesRomId.value();
    scenario_->setConfig(scenarioConfig, *world_);
}

void TrainingRunner::runScenarioDrivenStep()
{
    DIRTSIM_ASSERT(world_, "TrainingRunner: World must exist before stepping");
    DIRTSIM_ASSERT(scenario_, "TrainingRunner: Scenario must exist before stepping");

    auto* nesScenario = dynamic_cast<NesScenario*>(scenario_.get());
    if (!nesScenario) {
        state_ = State::OrganismDied;
        return;
    }

    if (!nesScenario->isRuntimeRunning() || !nesScenario->isRuntimeHealthy()) {
        state_ = State::OrganismDied;
        return;
    }

    const uint64_t renderedFramesBefore = nesScenario->getRuntimeRenderedFrameCount();
    uint8_t controllerMask = inferNesControllerMask();
    constexpr uint8_t kNesButtonStart = (1u << 3);
    if (nesFramesSurvived_ < 5) {
        controllerMask |= kNesButtonStart;
    }
    nesControllerMask_ = controllerMask;
    nesScenario->setController1State(nesControllerMask_);

    world_->advanceTime(TIMESTEP);
    simTime_ += TIMESTEP;

    const uint64_t renderedFramesAfter = nesScenario->getRuntimeRenderedFrameCount();
    if (renderedFramesAfter > renderedFramesBefore) {
        const uint64_t advancedFrames = renderedFramesAfter - renderedFramesBefore;
        nesFramesSurvived_ += advancedFrames;
        nesRewardTotal_ += static_cast<double>(advancedFrames);
    }

    if (!nesScenario->isRuntimeRunning() || !nesScenario->isRuntimeHealthy()) {
        state_ = State::OrganismDied;
    }
}

uint8_t TrainingRunner::inferNesControllerMask() const
{
    if (!individual_.genome.has_value()
        || individual_.genome->weights.size() != NES_POLICY_WEIGHT_COUNT) {
        return 0;
    }

    std::array<float, NES_POLICY_INPUT_COUNT> inputs{};
    inputs.fill(0.0f);

    const auto& worldData = world_->getData();
    if (worldData.scenario_video_frame.has_value()) {
        const ScenarioVideoFrame& frame = worldData.scenario_video_frame.value();
        const size_t pixelCount =
            static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
        const size_t expectedBytes = pixelCount * 2u;
        if (frame.width > 0 && frame.height > 0 && frame.pixels.size() >= expectedBytes) {
            for (int outY = 0; outY < NES_POLICY_DOWNSAMPLED_HEIGHT; ++outY) {
                const uint16_t srcY = static_cast<uint16_t>(
                    (static_cast<uint32_t>(outY) * frame.height) / NES_POLICY_DOWNSAMPLED_HEIGHT);
                for (int outX = 0; outX < NES_POLICY_DOWNSAMPLED_WIDTH; ++outX) {
                    const uint16_t srcX = static_cast<uint16_t>(
                        (static_cast<uint32_t>(outX) * frame.width) / NES_POLICY_DOWNSAMPLED_WIDTH);
                    const size_t pixelIndex =
                        static_cast<size_t>(srcY) * static_cast<size_t>(frame.width) + srcX;
                    const size_t byteIndex = pixelIndex * 2u;
                    if ((byteIndex + 1u) >= frame.pixels.size()) {
                        continue;
                    }

                    const float luma =
                        decodeRgb565Luma(frame.pixels[byteIndex], frame.pixels[byteIndex + 1u]);
                    const size_t inputIndex =
                        static_cast<size_t>(outY * NES_POLICY_DOWNSAMPLED_WIDTH + outX);
                    inputs[inputIndex] = luma;
                }
            }
        }
    }

    const float progress = maxTime_ > 0.0 ? static_cast<float>(simTime_ / maxTime_) : 0.0f;
    inputs[NES_POLICY_INPUT_COUNT - 2] = std::clamp(progress, 0.0f, 1.0f);
    inputs[NES_POLICY_INPUT_COUNT - 1] = (nesControllerMask_ & 0x01u) != 0u ? 1.0f : 0.0f;

    const auto& weights = individual_.genome->weights;
    const size_t outputBiasOffset =
        static_cast<size_t>(NES_POLICY_INPUT_COUNT) * static_cast<size_t>(NES_POLICY_OUTPUT_COUNT);

    uint8_t mask = 0;
    for (int outputIndex = 0; outputIndex < NES_POLICY_OUTPUT_COUNT; ++outputIndex) {
        float sum = weights[outputBiasOffset + static_cast<size_t>(outputIndex)];
        const size_t outputWeightOffset =
            static_cast<size_t>(outputIndex) * static_cast<size_t>(NES_POLICY_INPUT_COUNT);
        for (int inputIndex = 0; inputIndex < NES_POLICY_INPUT_COUNT; ++inputIndex) {
            sum += weights[outputWeightOffset + static_cast<size_t>(inputIndex)]
                * inputs[static_cast<size_t>(inputIndex)];
        }

        if (sum > 0.0f) {
            mask |= static_cast<uint8_t>(1u << outputIndex);
        }
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
