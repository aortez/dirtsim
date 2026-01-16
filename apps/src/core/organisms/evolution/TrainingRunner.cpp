#include "TrainingRunner.h"
#include "EvolutionConfig.h"
#include "GenomeRepository.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include <limits>
#include <optional>

namespace DirtSim {

namespace {
std::optional<Vector2i> findNearestSpawnableInRows(
    World& world, Vector2i requested, int startY, int endY)
{
    auto& data = world.getData();
    const int width = data.width;

    if (startY > endY) {
        return std::nullopt;
    }

    const auto isSpawnable = [&world, &data](int cellX, int cellY) {
        if (!data.inBounds(cellX, cellY)) {
            return false;
        }
        if (!data.at(cellX, cellY).isAir()) {
            return false;
        }
        return !world.getOrganismManager().hasOrganism({ cellX, cellY });
    };

    long long bestDistance = std::numeric_limits<long long>::max();
    Vector2i best{ 0, 0 };
    bool found = false;

    for (int y = startY; y <= endY; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!isSpawnable(x, y)) {
                continue;
            }
            const long long dx = static_cast<long long>(x) - requested.x;
            const long long dy = static_cast<long long>(y) - requested.y;
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
}

Vector2i resolveTreeSpawnCell(World& world, Vector2i requested)
{
    auto& data = world.getData();
    const int height = data.height;

    if (auto top = findNearestSpawnableInRows(world, requested, 0, requested.y); top.has_value()) {
        return top.value();
    }

    if (auto bottom = findNearestSpawnableInRows(world, requested, requested.y + 1, height - 1);
        bottom.has_value()) {
        return bottom.value();
    }

    if (world.getOrganismManager().hasOrganism({ requested.x, requested.y })) {
        DIRTSIM_ASSERT(false, "TrainingRunner: Spawn location already occupied");
    }

    data.at(requested.x, requested.y).clear();
    return requested;
}
} // namespace

TrainingRunner::TrainingRunner(
    const Genome& genome,
    Scenario::EnumType scenarioId,
    const EvolutionConfig& config,
    GenomeRepository& genomeRepository)
    : genome_(genome), maxTime_(config.maxSimulationTime)
{
    // Create scenario from registry.
    auto registry = ScenarioRegistry::createDefault(genomeRepository);
    scenario_ = registry.createScenario(scenarioId);

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

TrainingRunner::Status TrainingRunner::step(int frames)
{
    if (state_ != State::Running) {
        return getStatus();
    }

    if (treeId_ == INVALID_ORGANISM_ID) {
        spawnEvaluationTree();
    }

    for (int i = 0; i < frames && state_ == State::Running; ++i) {
        world_->advanceTime(TIMESTEP);
        simTime_ += TIMESTEP;

        // Track tree metrics.
        Tree* tree = world_->getOrganismManager().getTree(treeId_);
        if (!tree) {
            state_ = State::TreeDied;
            break;
        }
        maxEnergy_ = std::max(maxEnergy_, tree->getEnergy());

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
    status.maxEnergy = maxEnergy_;

    if (const Tree* tree = getTree()) {
        status.lifespan = tree->getAge();
    }
    else {
        status.lifespan = simTime_;
    }

    return status;
}

const Tree* TrainingRunner::getTree() const
{
    if (!world_) {
        return nullptr;
    }
    return world_->getOrganismManager().getTree(treeId_);
}

bool TrainingRunner::isTreeAlive() const
{
    return getTree() != nullptr;
}

void TrainingRunner::spawnEvaluationTree()
{
    DIRTSIM_ASSERT(world_, "TrainingRunner: World must exist before spawn");

    const int centerX = world_->getData().width / 2;
    const int centerY = world_->getData().height / 2;
    const Vector2i requested{ centerX, centerY };
    const Vector2i spawnCell = resolveTreeSpawnCell(*world_, requested);

    auto brain = std::make_unique<NeuralNetBrain>(genome_);
    treeId_ = world_->getOrganismManager().createTree(
        *world_, spawnCell.x, spawnCell.y, std::move(brain));
    DIRTSIM_ASSERT(treeId_ != INVALID_ORGANISM_ID, "TrainingRunner: Spawn failed");
}

} // namespace DirtSim
