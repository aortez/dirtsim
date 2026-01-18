#include "TrainingBrainRegistry.h"
#include "core/Assert.h"
#include "core/World.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/GooseBrain.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/brains/RuleBased2Brain.h"
#include "core/organisms/brains/RuleBasedBrain.h"

namespace DirtSim {

void TrainingBrainRegistry::registerBrain(
    OrganismType organismType,
    const std::string& brainKind,
    const std::string& brainVariant,
    BrainRegistryEntry entry)
{
    DIRTSIM_ASSERT(!brainKind.empty(), "TrainingBrainRegistry: brainKind must not be empty");
    DIRTSIM_ASSERT(entry.spawn, "TrainingBrainRegistry: spawn function must be set");

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
        });

    return registry;
}

} // namespace DirtSim
