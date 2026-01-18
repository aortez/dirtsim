#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/TreeBrain.h"
#include "core/organisms/TreeCommandProcessor.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/brains/RuleBased2Brain.h"
#include "core/organisms/brains/RuleBasedBrain.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/FitnessCalculator.h"
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingRunner.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include <array>
#include <gtest/gtest.h>
#include <iostream>
#include <random>
#include <sstream>
#include <unordered_map>

namespace DirtSim {

class TrainingRunnerTest : public ::testing::Test {
protected:
    void SetUp() override { rng_.seed(std::random_device{}()); }

    std::mt19937 rng_;
    EvolutionConfig config_;
    GenomeRepository genomeRepository_;
};

namespace {

class TestTreeBrain : public TreeBrain {
public:
    explicit TestTreeBrain(bool* decided) : decided_(decided) {}

    TreeCommand decide(const TreeSensoryData& /*sensory*/) override
    {
        if (decided_) {
            *decided_ = true;
        }
        return WaitCommand{};
    }

private:
    bool* decided_ = nullptr;
};

class RecordingTreeBrain : public TreeBrain {
public:
    RecordingTreeBrain(std::unique_ptr<TreeBrain> inner, std::vector<TreeCommand>* issued)
        : inner_(std::move(inner)), issued_(issued)
    {}

    TreeCommand decide(const TreeSensoryData& sensory) override
    {
        TreeCommand command = inner_->decide(sensory);
        if (issued_) {
            issued_->push_back(command);
        }
        return command;
    }

private:
    std::unique_ptr<TreeBrain> inner_;
    std::vector<TreeCommand>* issued_ = nullptr;
};

struct ExecutedCommand {
    TreeCommand command;
    CommandExecutionResult result;
};

class RecordingCommandProcessor : public ITreeCommandProcessor {
public:
    RecordingCommandProcessor(
        std::unique_ptr<ITreeCommandProcessor> inner, std::vector<ExecutedCommand>* executed)
        : inner_(std::move(inner)), executed_(executed)
    {}

    CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) override
    {
        CommandExecutionResult result = inner_->execute(tree, world, cmd);
        if (executed_) {
            executed_->push_back(ExecutedCommand{ cmd, result });
        }
        return result;
    }

private:
    std::unique_ptr<ITreeCommandProcessor> inner_;
    std::vector<ExecutedCommand>* executed_ = nullptr;
};

const char* commandTypeName(TreeCommandType type)
{
    switch (type) {
        case TreeCommandType::WaitCommand:
            return "Wait";
        case TreeCommandType::CancelCommand:
            return "Cancel";
        case TreeCommandType::GrowWoodCommand:
            return "GrowWood";
        case TreeCommandType::GrowLeafCommand:
            return "GrowLeaf";
        case TreeCommandType::GrowRootCommand:
            return "GrowRoot";
        case TreeCommandType::ReinforceCellCommand:
            return "ReinforceCell";
        case TreeCommandType::ProduceSeedCommand:
            return "ProduceSeed";
    }
    return "Unknown";
}

const char* commandResultName(CommandResult result)
{
    switch (result) {
        case CommandResult::SUCCESS:
            return "SUCCESS";
        case CommandResult::INSUFFICIENT_ENERGY:
            return "INSUFFICIENT_ENERGY";
        case CommandResult::INVALID_TARGET:
            return "INVALID_TARGET";
        case CommandResult::BLOCKED:
            return "BLOCKED";
    }
    return "UNKNOWN";
}

std::string formatCommand(const TreeCommand& command)
{
    return std::visit(
        [](const auto& cmd) -> std::string {
            using T = std::decay_t<decltype(cmd)>;
            std::ostringstream out;
            if constexpr (std::is_same_v<T, WaitCommand>) {
                out << "Wait";
            }
            else if constexpr (std::is_same_v<T, CancelCommand>) {
                out << "Cancel";
            }
            else if constexpr (std::is_same_v<T, GrowWoodCommand>) {
                out << "GrowWood (" << cmd.target_pos.x << "," << cmd.target_pos.y
                    << ") t=" << cmd.execution_time_seconds;
            }
            else if constexpr (std::is_same_v<T, GrowLeafCommand>) {
                out << "GrowLeaf (" << cmd.target_pos.x << "," << cmd.target_pos.y
                    << ") t=" << cmd.execution_time_seconds;
            }
            else if constexpr (std::is_same_v<T, GrowRootCommand>) {
                out << "GrowRoot (" << cmd.target_pos.x << "," << cmd.target_pos.y
                    << ") t=" << cmd.execution_time_seconds;
            }
            else if constexpr (std::is_same_v<T, ReinforceCellCommand>) {
                out << "ReinforceCell (" << cmd.position.x << "," << cmd.position.y
                    << ") t=" << cmd.execution_time_seconds;
            }
            else if constexpr (std::is_same_v<T, ProduceSeedCommand>) {
                out << "ProduceSeed (" << cmd.position.x << "," << cmd.position.y
                    << ") t=" << cmd.execution_time_seconds;
            }
            return out.str();
        },
        command);
}

std::array<int, NUM_TREE_COMMAND_TYPES> countCommands(const std::vector<TreeCommand>& commands)
{
    std::array<int, NUM_TREE_COMMAND_TYPES> counts{};
    counts.fill(0);
    for (const auto& cmd : commands) {
        const auto type = getCommandType(cmd);
        counts[static_cast<size_t>(type)]++;
    }
    return counts;
}

std::array<int, NUM_TREE_COMMAND_TYPES> countExecutedCommands(
    const std::vector<ExecutedCommand>& commands)
{
    std::array<int, NUM_TREE_COMMAND_TYPES> counts{};
    counts.fill(0);
    for (const auto& cmd : commands) {
        const auto type = getCommandType(cmd.command);
        counts[static_cast<size_t>(type)]++;
    }
    return counts;
}

void printCommandSummary(
    const std::string& label, const std::array<int, NUM_TREE_COMMAND_TYPES>& counts)
{
    std::cout << label << "\n";
    for (size_t i = 0; i < counts.size(); ++i) {
        const auto type = static_cast<TreeCommandType>(i);
        std::cout << "  - " << commandTypeName(type) << ": " << counts[i] << "\n";
    }
}

template <typename KeyFunc>
void printRolledUpList(
    const std::string& label, const std::vector<TreeCommand>& commands, KeyFunc&& keyFunc)
{
    std::vector<std::pair<std::string, int>> rolled;
    std::unordered_map<std::string, size_t> indexByKey;
    for (const auto& cmd : commands) {
        std::string key = keyFunc(cmd);
        auto it = indexByKey.find(key);
        if (it == indexByKey.end()) {
            indexByKey.emplace(key, rolled.size());
            rolled.push_back({ key, 1 });
        }
        else {
            rolled[it->second].second++;
        }
    }

    std::cout << label << " (" << commands.size() << ")\n";
    for (const auto& entry : rolled) {
        std::cout << "  - " << entry.first << " x" << entry.second << "\n";
    }
}

void printCommandListRolledUp(const std::string& label, const std::vector<TreeCommand>& commands)
{
    printRolledUpList(label, commands, [](const TreeCommand& cmd) { return formatCommand(cmd); });
}

void printExecutedCommandListRolledUp(
    const std::string& label, const std::vector<ExecutedCommand>& commands)
{
    std::vector<TreeCommand> flattened;
    flattened.reserve(commands.size());
    std::vector<std::string> results;
    results.reserve(commands.size());

    for (const auto& entry : commands) {
        flattened.push_back(entry.command);
        results.push_back(commandResultName(entry.result.result));
    }

    std::vector<std::pair<std::string, int>> rolled;
    std::unordered_map<std::string, size_t> indexByKey;
    for (size_t i = 0; i < commands.size(); ++i) {
        std::string key = formatCommand(flattened[i]) + " -> " + results[i];
        auto it = indexByKey.find(key);
        if (it == indexByKey.end()) {
            indexByKey.emplace(key, rolled.size());
            rolled.push_back({ key, 1 });
        }
        else {
            rolled[it->second].second++;
        }
    }

    std::cout << label << " (" << commands.size() << ")\n";
    for (const auto& entry : rolled) {
        std::cout << "  - " << entry.first << " x" << entry.second << "\n";
    }
}

} // namespace

// Proves the core design - that we can step incrementally without blocking.
TEST_F(TrainingRunnerTest, StepIsIncrementalNotBlocking)
{
    config_.maxSimulationTime = 1.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);

    // Step once - should return quickly, still running.
    auto status1 = runner.step(1);
    EXPECT_EQ(status1.state, TrainingRunner::State::Running);
    EXPECT_NEAR(runner.getSimTime(), 0.016, 0.001);

    // Step again - time accumulates, still running.
    auto status2 = runner.step(1);
    EXPECT_EQ(status2.state, TrainingRunner::State::Running);
    EXPECT_NEAR(runner.getSimTime(), 0.032, 0.001);

    // World exists and is accessible between steps.
    EXPECT_NE(runner.getWorld(), nullptr);
}

// Proves we can finish and get results.
TEST_F(TrainingRunnerTest, CompletionReturnsFitnessResults)
{
    config_.maxSimulationTime = 0.048; // 3 frames - quick completion.

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);

    // Step until complete.
    TrainingRunner::Status status;
    int steps = 0;
    while ((status = runner.step(1)).state == TrainingRunner::State::Running) {
        steps++;
        ASSERT_LT(steps, 100) << "Should complete within reasonable steps";
    }

    // Verify completion state.
    EXPECT_EQ(status.state, TrainingRunner::State::TimeExpired);

    // Verify fitness metrics are populated.
    EXPECT_NEAR(status.lifespan, config_.maxSimulationTime, 0.02);
    EXPECT_GE(status.distanceTraveled, 0.0);
    EXPECT_GE(status.maxEnergy, 0.0);
}

TEST_F(TrainingRunnerTest, UsesConfiguredBrainRegistry)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = "TestBrain";

    bool decided = false;
    TrainingBrainRegistry registry;
    registry.registerBrain(
        OrganismType::TREE,
        "TestBrain",
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [&decided](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    return world.getOrganismManager().createTree(
                        world, x, y, std::make_unique<TestTreeBrain>(&decided));
                },
        });

    TrainingRunner::Config runnerConfig{ .brainRegistry = registry };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    runner.step(1);

    EXPECT_TRUE(decided);
}

TEST_F(TrainingRunnerTest, TreeScenarioBrainHarness)
{
    config_.maxSimulationTime = 100.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    struct BrainCase {
        std::string brainKind;
        std::optional<Genome> genome;
    };

    std::vector<BrainCase> brains;
    brains.push_back({ TrainingBrainKind::NeuralNet, Genome::random(rng_) });
    brains.push_back({ TrainingBrainKind::RuleBased, std::nullopt });
    brains.push_back({ TrainingBrainKind::RuleBased2, std::nullopt });

    for (const auto& brainCase : brains) {
        std::vector<TreeCommand> issued;
        std::vector<TreeCommand> accepted;
        std::vector<ExecutedCommand> executed;
        std::optional<std::string> lastAccepted;
        bool processorWrapped = false;

        TrainingBrainRegistry registry;
        if (brainCase.brainKind == TrainingBrainKind::NeuralNet) {
            registry.registerBrain(
                OrganismType::TREE,
                brainCase.brainKind,
                "",
                BrainRegistryEntry{
                    .requiresGenome = true,
                    .allowsMutation = true,
                    .spawn =
                        [&issued](World& world, uint32_t x, uint32_t y, const Genome* genome) {
                            auto baseBrain = std::make_unique<NeuralNetBrain>(*genome);
                            auto brain =
                                std::make_unique<RecordingTreeBrain>(std::move(baseBrain), &issued);
                            return world.getOrganismManager().createTree(
                                world, x, y, std::move(brain));
                        },
                });
        }
        else if (brainCase.brainKind == TrainingBrainKind::RuleBased) {
            registry.registerBrain(
                OrganismType::TREE,
                brainCase.brainKind,
                "",
                BrainRegistryEntry{
                    .requiresGenome = false,
                    .allowsMutation = false,
                    .spawn =
                        [&issued](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                            auto baseBrain = std::make_unique<RuleBasedBrain>();
                            auto brain =
                                std::make_unique<RecordingTreeBrain>(std::move(baseBrain), &issued);
                            return world.getOrganismManager().createTree(
                                world, x, y, std::move(brain));
                        },
                });
        }
        else if (brainCase.brainKind == TrainingBrainKind::RuleBased2) {
            registry.registerBrain(
                OrganismType::TREE,
                brainCase.brainKind,
                "",
                BrainRegistryEntry{
                    .requiresGenome = false,
                    .allowsMutation = false,
                    .spawn =
                        [&issued](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                            auto baseBrain = std::make_unique<RuleBased2Brain>();
                            auto brain =
                                std::make_unique<RecordingTreeBrain>(std::move(baseBrain), &issued);
                            return world.getOrganismManager().createTree(
                                world, x, y, std::move(brain));
                        },
                });
        }

        TrainingRunner::Individual individual;
        individual.brain.brainKind = brainCase.brainKind;
        individual.genome = brainCase.genome;

        TrainingRunner::Config runnerConfig{ .brainRegistry = registry };
        TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

        TrainingRunner::Status status;
        int steps = 0;
        while ((status = runner.step(1)).state == TrainingRunner::State::Running) {
            steps++;
            ASSERT_LT(steps, 10000) << "Harness should complete within reasonable steps";

            World* world = runner.getWorld();
            if (!world) {
                continue;
            }
            const Organism::Body* organism = runner.getOrganism();
            if (!organism || organism->getType() != OrganismType::TREE) {
                continue;
            }
            Tree* tree = world->getOrganismManager().getTree(organism->getId());
            if (!tree) {
                continue;
            }

            if (!processorWrapped && tree->processor) {
                auto wrapped = std::make_unique<RecordingCommandProcessor>(
                    std::move(tree->processor), &executed);
                tree->processor = std::move(wrapped);
                processorWrapped = true;
            }

            const auto& current = tree->getCurrentCommand();
            if (current.has_value()) {
                std::string currentKey = formatCommand(*current);
                if (!lastAccepted.has_value() || *lastAccepted != currentKey) {
                    accepted.push_back(*current);
                    lastAccepted = currentKey;
                }
            }
            else {
                lastAccepted.reset();
            }
        }

        const World* world = runner.getWorld();
        ASSERT_NE(world, nullptr);

        const FitnessResult fitnessResult{
            .lifespan = status.lifespan,
            .distanceTraveled = status.distanceTraveled,
            .maxEnergy = status.maxEnergy,
        };
        const double fitness = computeFitnessForOrganism(
            fitnessResult,
            spec.organismType,
            world->getData().width,
            world->getData().height,
            config_);

        std::cout << "\n=== Brain Harness: " << brainCase.brainKind << " ===\n";
        std::cout << "Fitness: " << fitness << "\n";
        std::cout << "Final world:\n"
                  << WorldDiagramGeneratorEmoji::generateAnsiDiagram(*world, true, true) << "\n";
        printCommandSummary("Issued summary", countCommands(issued));
        printCommandSummary("Accepted summary", countCommands(accepted));
        printCommandSummary("Executed summary", countExecutedCommands(executed));
        printCommandListRolledUp("Issued commands", issued);
        printCommandListRolledUp("Accepted commands", accepted);
        printExecutedCommandListRolledUp("Executed commands", executed);
    }
}

TEST_F(TrainingRunnerTest, SpawnPrefersNearestAirInTopHalf)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);
    World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);

    auto& data = world->getData();
    const int centerX = data.width / 2;
    const int centerY = data.height / 2;
    const int width = data.width;
    const int height = data.height;

    for (int y = 0; y <= centerY; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }
    for (int y = centerY + 1; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }

    const int expectedX = centerX - 1;
    const int expectedY = centerY - 1;
    const int fartherX = centerX - 3;
    const int fartherY = centerY;
    const int bottomX = centerX;
    const int bottomY = centerY + 1;

    data.at(expectedX, expectedY).clear();
    data.at(fartherX, fartherY).clear();
    data.at(bottomX, bottomY).clear();

    runner.step(0);

    EXPECT_TRUE(world->getOrganismManager().hasOrganism({ expectedX, expectedY }));
    EXPECT_FALSE(world->getOrganismManager().hasOrganism({ bottomX, bottomY }));
}

TEST_F(TrainingRunnerTest, SpawnFallsBackToBottomHalfWhenTopHalfIsFull)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);
    World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);

    auto& data = world->getData();
    const int centerX = data.width / 2;
    const int centerY = data.height / 2;
    const int width = data.width;
    const int height = data.height;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }

    const int bottomX = centerX + 1;
    const int bottomY = centerY + 1;
    data.at(bottomX, bottomY).clear();

    runner.step(0);

    EXPECT_TRUE(world->getOrganismManager().hasOrganism({ bottomX, bottomY }));
}

} // namespace DirtSim
