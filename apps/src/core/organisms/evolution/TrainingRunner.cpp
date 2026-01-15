#include "TrainingRunner.h"
#include "EvolutionConfig.h"
#include "GenomeRepository.h"
#include "core/Assert.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"

namespace DirtSim {

TrainingRunner::TrainingRunner(
    const TrainingSpec& trainingSpec,
    const Individual& individual,
    const EvolutionConfig& config,
    GenomeRepository& genomeRepository)
    : trainingSpec_(trainingSpec),
      individual_(individual),
      maxTime_(config.maxSimulationTime),
      brainRegistry_(TrainingBrainRegistry::createDefault())
{
    // Create scenario from registry.
    auto registry = ScenarioRegistry::createDefault(genomeRepository);
    scenario_ = registry.createScenario(trainingSpec_.scenarioId);
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
                maxEnergy_ = std::max(maxEnergy_, tree->getEnergy());
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
    status.maxEnergy = maxEnergy_;

    if (const Organism::Body* organism = getOrganism()) {
        status.lifespan = organism->getAge();
    }
    else {
        status.lifespan = simTime_;
    }

    return status;
}

const Organism::Body* TrainingRunner::getOrganism() const
{
    if (!world_) {
        return nullptr;
    }
    return world_->getOrganismManager().getOrganism(organismId_);
}

bool TrainingRunner::isOrganismAlive() const
{
    return getOrganism() != nullptr;
}

void TrainingRunner::spawnEvaluationOrganism()
{
    DIRTSIM_ASSERT(world_, "TrainingRunner: World must exist before spawn");
    DIRTSIM_ASSERT(scenario_, "TrainingRunner: Scenario must exist before spawn");

    const int centerX = world_->getData().width / 2;
    const int centerY = world_->getData().height / 2;
    Vector2i center{ centerX, centerY };

    if (world_->getOrganismManager().hasOrganism(center)) {
        DIRTSIM_ASSERT(false, "TrainingRunner: Spawn location already occupied");
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

    organismId_ = entry->spawn(*world_, centerX, centerY, genomePtr);
    DIRTSIM_ASSERT(organismId_ != INVALID_ORGANISM_ID, "TrainingRunner: Spawn failed");

    const Organism::Body* organism = world_->getOrganismManager().getOrganism(organismId_);
    DIRTSIM_ASSERT(organism != nullptr, "TrainingRunner: Spawned organism not found");
    spawnPosition_ = organism->position;
    lastPosition_ = spawnPosition_;
}

} // namespace DirtSim
