#include "TrainingBrainRegistry.h"
#include "core/Assert.h"
#include "core/World.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/GooseBrain.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/brains/RuleBased2Brain.h"
#include "core/organisms/brains/RuleBasedBrain.h"
#include "core/scenarios/nes/NesTileRecurrentBrain.h"

namespace DirtSim {

void TrainingBrainRegistry::registerBrain(
    OrganismType organismType,
    const std::string& brainKind,
    const std::string& brainVariant,
    BrainRegistryEntry entry)
{
    DIRTSIM_ASSERT(!brainKind.empty(), "TrainingBrainRegistry: brainKind must not be empty");
    if (entry.controlMode == BrainRegistryEntry::ControlMode::OrganismDriven) {
        DIRTSIM_ASSERT(entry.spawn, "TrainingBrainRegistry: spawn function must be set");
    }
    else {
        DIRTSIM_ASSERT(
            !entry.spawn,
            "TrainingBrainRegistry: spawn function must be unset for scenario-driven brains");
    }
    if (entry.requiresGenome) {
        DIRTSIM_ASSERT(
            entry.createRandomGenome, "TrainingBrainRegistry: requiresGenome requires generator");
        DIRTSIM_ASSERT(
            entry.isGenomeCompatible,
            "TrainingBrainRegistry: requiresGenome requires compatibility check");
        DIRTSIM_ASSERT(
            entry.getGenomeLayout,
            "TrainingBrainRegistry: requiresGenome requires getGenomeLayout");
    }
    else {
        DIRTSIM_ASSERT(
            !entry.createRandomGenome,
            "TrainingBrainRegistry: createRandomGenome must be unset when requiresGenome=false");
        DIRTSIM_ASSERT(
            !entry.isGenomeCompatible,
            "TrainingBrainRegistry: isGenomeCompatible must be unset when requiresGenome=false");
        DIRTSIM_ASSERT(
            !entry.getGenomeLayout,
            "TrainingBrainRegistry: getGenomeLayout must be unset when requiresGenome=false");
    }

    BrainRegistryKey key{ .organismType = organismType,
                          .brainKind = brainKind,
                          .brainVariant = brainVariant };
    entries_[key] = std::move(entry);
}

const BrainRegistryEntry* TrainingBrainRegistry::find(
    OrganismType organismType, const std::string& brainKind, const std::string& brainVariant) const
{
    BrainRegistryKey key{ .organismType = organismType,
                          .brainKind = brainKind,
                          .brainVariant = brainVariant };
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

TrainingBrainRegistry TrainingBrainRegistry::createDefault()
{
    TrainingBrainRegistry registry;

    registry.registerBrain(
        OrganismType::TREE,
        TrainingBrainKind::NeuralNet,
        "",
        BrainRegistryEntry{
            .requiresGenome = true,
            .allowsMutation = true,
            .spawn = [](World& world, uint32_t x, uint32_t y, const Genome* genome) -> OrganismId {
                DIRTSIM_ASSERT(genome != nullptr, "NeuralNet brain requires a genome");
                auto brain = std::make_unique<NeuralNetBrain>(*genome);
                return world.getOrganismManager().createTree(world, x, y, std::move(brain));
            },
            .createRandomGenome =
                [](std::mt19937& rng) { return NeuralNetBrain::randomGenome(rng); },
            .isGenomeCompatible =
                [](const Genome& genome) { return NeuralNetBrain::isGenomeCompatible(genome); },
            .getGenomeLayout = []() { return NeuralNetBrain::getGenomeLayout(); },
        });

    registry.registerBrain(
        OrganismType::DUCK,
        TrainingBrainKind::DuckNeuralNetRecurrentV2,
        "",
        BrainRegistryEntry{
            .requiresGenome = true,
            .allowsMutation = true,
            .spawn = [](World& world, uint32_t x, uint32_t y, const Genome* genome) -> OrganismId {
                DIRTSIM_ASSERT(
                    genome != nullptr, "DuckNeuralNetRecurrentV2 brain requires a genome");
                auto brain = std::make_unique<DuckNeuralNetRecurrentBrainV2>(*genome);
                return world.getOrganismManager().createDuck(world, x, y, std::move(brain));
            },
            .createRandomGenome =
                [](std::mt19937& rng) { return DuckNeuralNetRecurrentBrainV2::randomGenome(rng); },
            .isGenomeCompatible =
                [](const Genome& genome) {
                    return DuckNeuralNetRecurrentBrainV2::isGenomeCompatible(genome);
                },
            .getGenomeLayout = []() { return DuckNeuralNetRecurrentBrainV2::getGenomeLayout(); },
        });

    registry.registerBrain(
        OrganismType::TREE,
        TrainingBrainKind::RuleBased,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<RuleBasedBrain>();
                    return world.getOrganismManager().createTree(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    registry.registerBrain(
        OrganismType::TREE,
        TrainingBrainKind::RuleBased2,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<RuleBased2Brain>();
                    return world.getOrganismManager().createTree(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    registry.registerBrain(
        OrganismType::DUCK,
        TrainingBrainKind::Random,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<RandomDuckBrain>();
                    return world.getOrganismManager().createDuck(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    registry.registerBrain(
        OrganismType::DUCK,
        TrainingBrainKind::WallBouncing,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<WallBouncingBrain>();
                    return world.getOrganismManager().createDuck(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    registry.registerBrain(
        OrganismType::DUCK,
        TrainingBrainKind::DuckBrain2,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<DuckBrain2>();
                    return world.getOrganismManager().createDuck(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    registry.registerBrain(
        OrganismType::GOOSE,
        TrainingBrainKind::Random,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<RandomGooseBrain>();
                    return world.getOrganismManager().createGoose(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    registry.registerBrain(
        OrganismType::NES_DUCK,
        TrainingBrainKind::DuckNeuralNetRecurrentV2,
        "",
        BrainRegistryEntry{
            .controlMode = BrainRegistryEntry::ControlMode::ScenarioDriven,
            .requiresGenome = true,
            .allowsMutation = true,
            .spawn = nullptr,
            .createRandomGenome =
                [](std::mt19937& rng) { return DuckNeuralNetRecurrentBrainV2::randomGenome(rng); },
            .isGenomeCompatible =
                [](const Genome& genome) {
                    return DuckNeuralNetRecurrentBrainV2::isGenomeCompatible(genome);
                },
            .getGenomeLayout = []() { return DuckNeuralNetRecurrentBrainV2::getGenomeLayout(); },
        });

    registry.registerBrain(
        OrganismType::NES_DUCK,
        TrainingBrainKind::NesTileRecurrent,
        "",
        BrainRegistryEntry{
            .controlMode = BrainRegistryEntry::ControlMode::ScenarioDriven,
            .requiresGenome = true,
            .allowsMutation = true,
            .spawn = nullptr,
            .createRandomGenome =
                [](std::mt19937& rng) { return NesTileRecurrentBrain::randomGenome(rng); },
            .isGenomeCompatible =
                [](const Genome& genome) {
                    return NesTileRecurrentBrain::isGenomeCompatible(genome);
                },
            .getGenomeLayout = []() { return NesTileRecurrentBrain::getGenomeLayout(); },
        });

    return registry;
}

} // namespace DirtSim
