#include "TrainingBrainRegistry.h"
#include "core/Assert.h"
#include "core/World.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/GooseBrain.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/brains/DuckNeuralNetBrain.h"
#include "core/organisms/brains/DuckNeuralNetRecurrentBrain.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/brains/RuleBased2Brain.h"
#include "core/organisms/brains/RuleBasedBrain.h"

namespace DirtSim {

std::optional<TrainingBrainDefaults> getTrainingBrainDefaults(const std::string& brainKind)
{
    if (brainKind == TrainingBrainKind::NeuralNet) {
        return TrainingBrainDefaults{
            .defaultScenarioId = Scenario::EnumType::TreeGermination,
            .defaultNesRomId = std::nullopt,
        };
    }
    if (brainKind == TrainingBrainKind::DuckNeuralNetRecurrent
        || brainKind == TrainingBrainKind::Random || brainKind == TrainingBrainKind::WallBouncing
        || brainKind == TrainingBrainKind::DuckBrain2) {
        return TrainingBrainDefaults{
            .defaultScenarioId = Scenario::EnumType::Clock,
            .defaultNesRomId = std::nullopt,
        };
    }
    if (brainKind == TrainingBrainKind::NesFlappyBird) {
        return TrainingBrainDefaults{
            .defaultScenarioId = Scenario::EnumType::Nes,
            .defaultNesRomId = std::string{ "flappy-paratroopa-world-unl" },
        };
    }

    return std::nullopt;
}

void TrainingBrainRegistry::registerBrain(
    OrganismType organismType,
    const std::string& brainKind,
    const std::string& brainVariant,
    BrainRegistryEntry entry)
{
    DIRTSIM_ASSERT(!brainKind.empty(), "TrainingBrainRegistry: brainKind must not be empty");
    DIRTSIM_ASSERT(entry.spawn, "TrainingBrainRegistry: spawn function must be set");
    if (entry.requiresGenome) {
        DIRTSIM_ASSERT(
            entry.createRandomGenome, "TrainingBrainRegistry: requiresGenome requires generator");
        DIRTSIM_ASSERT(
            entry.isGenomeCompatible,
            "TrainingBrainRegistry: requiresGenome requires compatibility check");
    }
    else {
        DIRTSIM_ASSERT(
            !entry.createRandomGenome,
            "TrainingBrainRegistry: createRandomGenome must be unset when requiresGenome=false");
        DIRTSIM_ASSERT(
            !entry.isGenomeCompatible,
            "TrainingBrainRegistry: isGenomeCompatible must be unset when requiresGenome=false");
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
            .createRandomGenome = [](std::mt19937& rng) { return Genome::random(rng); },
            .isGenomeCompatible =
                [](const Genome& genome) {
                    return genome.weights.size() == Genome::EXPECTED_WEIGHT_COUNT;
                },
        });

    registry.registerBrain(
        OrganismType::DUCK,
        TrainingBrainKind::NeuralNet,
        "",
        BrainRegistryEntry{
            .requiresGenome = true,
            .allowsMutation = true,
            .spawn = [](World& world, uint32_t x, uint32_t y, const Genome* genome) -> OrganismId {
                DIRTSIM_ASSERT(genome != nullptr, "DuckNeuralNet brain requires a genome");
                auto brain = std::make_unique<DuckNeuralNetBrain>(*genome);
                return world.getOrganismManager().createDuck(world, x, y, std::move(brain));
            },
            .createRandomGenome =
                [](std::mt19937& rng) { return DuckNeuralNetBrain::randomGenome(rng); },
            .isGenomeCompatible =
                [](const Genome& genome) { return DuckNeuralNetBrain::isGenomeCompatible(genome); },
        });

    registry.registerBrain(
        OrganismType::DUCK,
        TrainingBrainKind::DuckNeuralNetRecurrent,
        "",
        BrainRegistryEntry{
            .requiresGenome = true,
            .allowsMutation = true,
            .spawn = [](World& world, uint32_t x, uint32_t y, const Genome* genome) -> OrganismId {
                DIRTSIM_ASSERT(genome != nullptr, "DuckNeuralNetRecurrent brain requires a genome");
                auto brain = std::make_unique<DuckNeuralNetRecurrentBrain>(*genome);
                return world.getOrganismManager().createDuck(world, x, y, std::move(brain));
            },
            .createRandomGenome =
                [](std::mt19937& rng) { return DuckNeuralNetRecurrentBrain::randomGenome(rng); },
            .isGenomeCompatible =
                [](const Genome& genome) {
                    return DuckNeuralNetRecurrentBrain::isGenomeCompatible(genome);
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
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
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
        });

    return registry;
}

} // namespace DirtSim
