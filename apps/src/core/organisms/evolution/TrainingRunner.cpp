#include "TrainingRunner.h"
#include "EvolutionConfig.h"
#include "FitnessCalculator.h"
#include "GenomeRepository.h"
#include "core/Assert.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include <limits>
#include <optional>

namespace DirtSim {

namespace {
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
      evolutionConfig_(evolutionConfig)
{
    // Create scenario from registry.
    auto registry = ScenarioRegistry::createDefault(genomeRepository);
    scenario_ = registry.createScenario(individual_.scenarioId);
    DIRTSIM_ASSERT(scenario_, "TrainingRunner: Scenario factory returned null");

    // Create world with scenario's required dimensions.
    const auto& metadata = scenario_->getMetadata();
    int width = metadata.requiredWidth > 0 ? metadata.requiredWidth : 9;
    int height = metadata.requiredHeight > 0 ? metadata.requiredHeight : 9;
    world_ = std::make_unique<World>(width, height);

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

    if (organismId_ == INVALID_ORGANISM_ID) {
        spawnEvaluationOrganism();
    }

    for (int i = 0; i < frames && state_ == State::Running; ++i) {
        world_->advanceTime(TIMESTEP);
        simTime_ += TIMESTEP;

        Organism::Body* organism = world_->getOrganismManager().getOrganism(organismId_);
        if (!organism) {
            state_ = State::OrganismDied;
            break;
        }
        lastPosition_ = organism->position;

        if (trainingSpec_.organismType == OrganismType::TREE) {
            Tree* tree = world_->getOrganismManager().getTree(organismId_);
            if (tree) {
                treeEvaluator_.update(*tree);
                const Vector2d delta{
                    lastPosition_.x - spawnPosition_.x,
                    lastPosition_.y - spawnPosition_.y,
                };
                const FitnessResult result{
                    .lifespan = tree->getAge(),
                    .distanceTraveled = delta.mag(),
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
    if (organismId_ != INVALID_ORGANISM_ID) {
        Vector2d delta{ lastPosition_.x - spawnPosition_.x, lastPosition_.y - spawnPosition_.y };
        status.distanceTraveled = delta.mag();
    }
    status.maxEnergy = treeEvaluator_.getMaxEnergy();
    status.commandsAccepted = treeEvaluator_.getCommandAcceptedCount();
    status.commandsRejected = treeEvaluator_.getCommandRejectedCount();
    status.idleCancels = treeEvaluator_.getIdleCancelCount();

    if (const Organism::Body* organism = getOrganism()) {
        status.lifespan = organism->getAge();
    }
    else {
        status.lifespan = simTime_;
    }

    return status;
}

const std::optional<TreeResourceTotals>& TrainingRunner::getTreeResourceTotals() const
{
    return treeEvaluator_.getResourceTotals();
}

std::vector<std::pair<std::string, int>> TrainingRunner::getTopCommandSignatures(
    size_t maxEntries) const
{
    if (!world_ || trainingSpec_.organismType != OrganismType::TREE) {
        return {};
    }

    const Tree* tree = world_->getOrganismManager().getTree(organismId_);
    if (!tree) {
        return {};
    }

    return tree->getTopCommandSignatures(maxEntries);
}

std::vector<std::pair<std::string, int>> TrainingRunner::getTopCommandOutcomeSignatures(
    size_t maxEntries) const
{
    if (!world_ || trainingSpec_.organismType != OrganismType::TREE) {
        return {};
    }

    const Tree* tree = world_->getOrganismManager().getTree(organismId_);
    if (!tree) {
        return {};
    }

    return tree->getTopCommandOutcomeSignatures(maxEntries);
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
    return getOrganism() != nullptr;
}

void TrainingRunner::spawnEvaluationOrganism()
{
    DIRTSIM_ASSERT(world_, "TrainingRunner: World must exist before spawn");
    DIRTSIM_ASSERT(scenario_, "TrainingRunner: Scenario must exist before spawn");

    const Vector2i spawnCell = findSpawnCell(*world_);

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
    spawnPosition_ = organism->position;
    lastPosition_ = spawnPosition_;
    if (trainingSpec_.organismType == OrganismType::TREE) {
        treeEvaluator_.start();
    }
}

} // namespace DirtSim
