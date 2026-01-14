#include "TrainingRunner.h"
#include "EvolutionConfig.h"
#include "GenomeRepository.h"
#include "core/World.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"

namespace DirtSim {

TrainingRunner::TrainingRunner(
    const Genome& genome,
    Scenario::EnumType scenarioId,
    const EvolutionConfig& config,
    GenomeRepository& genomeRepository)
    : maxTime_(config.maxSimulationTime)
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

    // Plant tree with neural net brain at center.
    auto brain = std::make_unique<NeuralNetBrain>(genome);
    int centerX = width / 2;
    int centerY = height / 2;
    treeId_ = world_->getOrganismManager().createTree(*world_, centerX, centerY, std::move(brain));
}

TrainingRunner::~TrainingRunner() = default;

TrainingRunner::TrainingRunner(TrainingRunner&&) noexcept = default;
TrainingRunner& TrainingRunner::operator=(TrainingRunner&&) noexcept = default;

TrainingRunner::Status TrainingRunner::step(int frames)
{
    if (state_ != State::Running) {
        return getStatus();
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

} // namespace DirtSim
