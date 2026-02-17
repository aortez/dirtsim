#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
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
#include "core/organisms/evolution/TreeEvaluator.h"
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <iomanip>
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

class ScriptedTreeBrain : public TreeBrain {
public:
    explicit ScriptedTreeBrain(std::vector<TreeCommand> commands) : commands_(std::move(commands))
    {}

    TreeCommand decide(const TreeSensoryData& /*sensory*/) override
    {
        if (nextCommandIndex_ < commands_.size()) {
            return commands_[nextCommandIndex_++];
        }
        return CancelCommand{};
    }

private:
    std::vector<TreeCommand> commands_;
    size_t nextCommandIndex_ = 0;
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

class ScriptedDuckBrain : public DuckBrain {
public:
    explicit ScriptedDuckBrain(std::vector<DuckInput> inputs) : inputs_(std::move(inputs)) {}

    void think(Duck& duck, const DuckSensoryData& /*sensory*/, double /*deltaTime*/) override
    {
        DuckInput input{};
        if (nextInputIndex_ < inputs_.size()) {
            input = inputs_[nextInputIndex_++];
        }

        if (input.jump) {
            current_action_ = DuckAction::JUMP;
        }
        else if (input.move.x < -0.01f) {
            current_action_ = DuckAction::RUN_LEFT;
        }
        else if (input.move.x > 0.01f) {
            current_action_ = DuckAction::RUN_RIGHT;
        }
        else {
            current_action_ = DuckAction::WAIT;
        }

        duck.setInput(input);
    }

private:
    std::vector<DuckInput> inputs_;
    size_t nextInputIndex_ = 0;
};

struct ExecutedCommand {
    TreeCommand command;
    CommandExecutionResult result;
};

struct ValidatedCommand {
    TreeCommand command;
    CommandExecutionResult result;
};

class RecordingCommandProcessor : public ITreeCommandProcessor {
public:
    RecordingCommandProcessor(
        std::unique_ptr<ITreeCommandProcessor> inner,
        std::vector<ValidatedCommand>* validated,
        std::vector<ExecutedCommand>* executed)
        : inner_(std::move(inner)), validated_(validated), executed_(executed)
    {}

    CommandExecutionResult validate(Tree& tree, World& world, const TreeCommand& cmd) override
    {
        CommandExecutionResult result = inner_->validate(tree, world, cmd);
        if (validated_) {
            validated_->push_back(ValidatedCommand{ cmd, result });
        }
        return result;
    }

    CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) override
    {
        CommandExecutionResult result = inner_->execute(tree, world, cmd);
        if (executed_) {
            executed_->push_back(ExecutedCommand{ cmd, result });
        }
        return result;
    }

    double getEnergyCost(const TreeCommand& cmd) const override
    {
        return inner_->getEnergyCost(cmd);
    }

private:
    std::unique_ptr<ITreeCommandProcessor> inner_;
    std::vector<ValidatedCommand>* validated_ = nullptr;
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

size_t commandResultIndex(CommandResult result)
{
    switch (result) {
        case CommandResult::SUCCESS:
            return 0;
        case CommandResult::INSUFFICIENT_ENERGY:
            return 1;
        case CommandResult::INVALID_TARGET:
            return 2;
        case CommandResult::BLOCKED:
            return 3;
    }
    return 0;
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

template <typename T>
std::array<int, NUM_TREE_COMMAND_TYPES> countCommandResults(const std::vector<T>& commands)
{
    std::array<int, NUM_TREE_COMMAND_TYPES> counts{};
    counts.fill(0);
    for (const auto& cmd : commands) {
        const auto type = getCommandType(cmd.command);
        counts[static_cast<size_t>(type)]++;
    }
    return counts;
}

template <typename T>
std::array<int, 4> countResultTypes(const std::vector<T>& commands)
{
    std::array<int, 4> counts{};
    counts.fill(0);
    for (const auto& cmd : commands) {
        counts[commandResultIndex(cmd.result.result)]++;
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

void printResultSummary(const std::string& label, const std::array<int, 4>& counts)
{
    std::cout << label << "\n";
    std::cout << "  - SUCCESS: " << counts[0] << "\n";
    std::cout << "  - INSUFFICIENT_ENERGY: " << counts[1] << "\n";
    std::cout << "  - INVALID_TARGET: " << counts[2] << "\n";
    std::cout << "  - BLOCKED: " << counts[3] << "\n";
}

template <typename T>
void printResultReasons(
    const std::string& label,
    const std::vector<T>& commands,
    CommandResult filter,
    size_t max_entries = 5)
{
    std::unordered_map<std::string, int> counts_by_message;
    int total = 0;
    for (const auto& entry : commands) {
        if (entry.result.result != filter) {
            continue;
        }
        counts_by_message[entry.result.message]++;
        total++;
    }

    if (total == 0) {
        return;
    }

    std::vector<std::pair<std::string, int>> rolled;
    rolled.reserve(counts_by_message.size());
    for (const auto& entry : counts_by_message) {
        rolled.push_back(entry);
    }
    std::sort(rolled.begin(), rolled.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::cout << label << " (" << total << ")\n";
    const size_t limit = std::min(max_entries, rolled.size());
    for (size_t i = 0; i < limit; ++i) {
        std::cout << "  - " << rolled[i].first << " x" << rolled[i].second << "\n";
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

template <typename T>
void printCommandResultsRolledUp(const std::string& label, const std::vector<T>& commands)
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

void printTreeFitnessBreakdown(const TreeFitnessBreakdown& breakdown)
{
    const double coreTerm =
        breakdown.survivalScore * (1.0 + breakdown.energyScore) * (1.0 + breakdown.resourceScore);
    const double bonusTerm = breakdown.partialStructureBonus + breakdown.stageBonus
        + breakdown.structureBonus + breakdown.milestoneBonus + breakdown.commandScore;

    const std::ios::fmtflags oldFlags = std::cout.flags();
    const std::streamsize oldPrecision = std::cout.precision();
    std::cout << std::fixed << std::setprecision(4);

    std::cout << "Fitness breakdown\n";
    std::cout << "  - survival: " << breakdown.survivalScore << "\n";
    std::cout << "  - energy: " << breakdown.energyScore << "\n";
    std::cout << "  - resource: " << breakdown.resourceScore << "\n";
    std::cout << "  - partial structure bonus: " << breakdown.partialStructureBonus << "\n";
    std::cout << "  - stage bonus: " << breakdown.stageBonus << "\n";
    std::cout << "  - structure bonus: " << breakdown.structureBonus << "\n";
    std::cout << "  - milestone bonus: " << breakdown.milestoneBonus << "\n";
    std::cout << "  - command score: " << breakdown.commandScore << "\n";
    std::cout << "  - core term: " << coreTerm << "\n";
    std::cout << "  - bonus term: " << bonusTerm << "\n";

    std::cout.flags(oldFlags);
    std::cout.precision(oldPrecision);
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
    EXPECT_FALSE(runner.getOrganismTrackingHistory().samples.empty());
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
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
        });

    TrainingRunner::Config runnerConfig{ .brainRegistry = registry };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    runner.step(1);

    EXPECT_TRUE(decided);
}

TEST_F(TrainingRunnerTest, TreeScenarioBrainHarness)
{
    config_.maxSimulationTime = 600.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    struct BrainCase {
        std::string brainKind;
        std::optional<Genome> genome;
    };

    std::vector<BrainCase> brains;
    brains.push_back({ TrainingBrainKind::NeuralNet, Genome::random(rng_) });
    brains.push_back({ TrainingBrainKind::NeuralNet, Genome::random(rng_) });
    brains.push_back({ TrainingBrainKind::NeuralNet, Genome::random(rng_) });
    brains.push_back({ TrainingBrainKind::RuleBased, std::nullopt });
    brains.push_back({ TrainingBrainKind::RuleBased2, std::nullopt });

    for (const auto& brainCase : brains) {
        std::vector<TreeCommand> issued;
        std::vector<TreeCommand> started;
        std::vector<ValidatedCommand> validated;
        std::vector<ExecutedCommand> executed;
        std::optional<std::string> lastStarted;
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
                    .createRandomGenome = [](std::mt19937& rng) { return Genome::random(rng); },
                    .isGenomeCompatible =
                        [](const Genome& genome) {
                            return genome.weights.size() == Genome::EXPECTED_WEIGHT_COUNT;
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
                    .createRandomGenome = nullptr,
                    .isGenomeCompatible = nullptr,
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
                    .createRandomGenome = nullptr,
                    .isGenomeCompatible = nullptr,
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
            ASSERT_LT(steps, 100000) << "Harness should complete within reasonable steps";

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
                    std::move(tree->processor), &validated, &executed);
                tree->processor = std::move(wrapped);
                processorWrapped = true;
            }

            const auto& current = tree->getCurrentCommand();
            if (current.has_value()) {
                std::string currentKey = formatCommand(*current);
                if (!lastStarted.has_value() || *lastStarted != currentKey) {
                    started.push_back(*current);
                    lastStarted = currentKey;
                }
            }
            else {
                lastStarted.reset();
            }
        }

        const World* world = runner.getWorld();
        ASSERT_NE(world, nullptr);

        const FitnessResult fitnessResult{
            .lifespan = status.lifespan,
            .maxEnergy = status.maxEnergy,
            .commandsAccepted = status.commandsAccepted,
            .commandsRejected = status.commandsRejected,
            .idleCancels = status.idleCancels,
        };
        const auto& treeResources = runner.getTreeResourceTotals();
        const TreeResourceTotals* treeResourcesPtr =
            treeResources.has_value() ? &treeResources.value() : nullptr;
        const FitnessContext context{
            .result = fitnessResult,
            .organismType = spec.organismType,
            .worldWidth = world->getData().width,
            .worldHeight = world->getData().height,
            .evolutionConfig = config_,
            .finalOrganism = runner.getOrganism(),
            .organismTrackingHistory = &runner.getOrganismTrackingHistory(),
            .treeResources = treeResourcesPtr,
        };
        const TreeFitnessBreakdown breakdown = TreeEvaluator::evaluateWithBreakdown(context);
        const double fitness = breakdown.totalFitness;

        std::cout << "\n=== Brain Harness: " << brainCase.brainKind << " ===\n";
        std::cout << "Fitness: " << fitness << "\n";
        printTreeFitnessBreakdown(breakdown);
        std::cout << "Command counters\n";
        std::cout << "  - accepted: " << status.commandsAccepted << "\n";
        std::cout << "  - rejected: " << status.commandsRejected << "\n";
        std::cout << "  - idle cancel: " << status.idleCancels << "\n";
        std::cout << "Final world:\n"
                  << WorldDiagramGeneratorEmoji::generateAnsiDiagram(*world, true, true) << "\n";
        printCommandSummary("Issued summary", countCommands(issued));
        printCommandSummary("Started summary", countCommands(started));
        printCommandSummary("Validated summary", countCommandResults(validated));
        printCommandSummary("Executed summary", countCommandResults(executed));
        printResultSummary("Validated result summary", countResultTypes(validated));
        printResultSummary("Executed result summary", countResultTypes(executed));
        printResultReasons(
            "Validated INVALID_TARGET reasons", validated, CommandResult::INVALID_TARGET);
        printResultReasons("Validated BLOCKED reasons", validated, CommandResult::BLOCKED);
        printResultReasons(
            "Validated INSUFFICIENT_ENERGY reasons", validated, CommandResult::INSUFFICIENT_ENERGY);
        printResultReasons(
            "Executed INVALID_TARGET reasons", executed, CommandResult::INVALID_TARGET);
        printResultReasons("Executed BLOCKED reasons", executed, CommandResult::BLOCKED);
        printResultReasons(
            "Executed INSUFFICIENT_ENERGY reasons", executed, CommandResult::INSUFFICIENT_ENERGY);
        printCommandListRolledUp("Issued commands", issued);
        printCommandListRolledUp("Started commands", started);
        printCommandResultsRolledUp("Validated commands", validated);
        printCommandResultsRolledUp("Executed commands", executed);
    }
}

TEST_F(TrainingRunnerTest, TopCommandSignaturesAreTop20AndTieBreakBySignature)
{
    config_.maxSimulationTime = 600.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    std::vector<TreeCommand> scriptedCommands;
    for (int i = 0; i < 7; ++i) {
        scriptedCommands.push_back(CancelCommand{});
    }
    for (int i = 0; i < 7; ++i) {
        scriptedCommands.push_back(WaitCommand{});
    }
    for (int i = 0; i < 25; ++i) {
        scriptedCommands.push_back(GrowLeafCommand{
            .target_pos = {
                static_cast<int16_t>(1000 + (i * 37)),
                static_cast<int16_t>(2000 + (i * 29)),
            },
            .execution_time_seconds = 0.5,
        });
    }

    std::vector<TreeCommand> issued;
    const std::string brainKind = "ScriptedTop20";
    TrainingBrainRegistry registry;
    registry.registerBrain(
        OrganismType::TREE,
        brainKind,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [&issued, &scriptedCommands](
                    World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto baseBrain = std::make_unique<ScriptedTreeBrain>(scriptedCommands);
                    auto brain =
                        std::make_unique<RecordingTreeBrain>(std::move(baseBrain), &issued);
                    return world.getOrganismManager().createTree(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;

    TrainingRunner::Config runnerConfig{ .brainRegistry = registry };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    int steps = 0;
    while (issued.size() < scriptedCommands.size()) {
        const TrainingRunner::Status status = runner.step(1);
        ++steps;
        ASSERT_LT(steps, 200000) << "Scripted commands should be issued promptly";
        ASSERT_EQ(status.state, TrainingRunner::State::Running)
            << "Runner ended before all scripted commands were issued";
    }

    const auto top20 = runner.getTopCommandSignatures(20);
    const auto top5 = runner.getTopCommandSignatures(5);
    const auto top1000 = runner.getTopCommandSignatures(1000);

    ASSERT_EQ(top20.size(), 20u);
    ASSERT_EQ(top5.size(), 5u);
    ASSERT_GT(top1000.size(), 20u);

    EXPECT_EQ(top20[0].first, "Cancel");
    EXPECT_EQ(top20[0].second, 7);
    EXPECT_EQ(top20[1].first, "Wait");
    EXPECT_EQ(top20[1].second, 7);

    for (size_t i = 0; i < top5.size(); ++i) {
        EXPECT_EQ(top5[i], top20[i]);
    }

    for (size_t i = 1; i < top20.size(); ++i) {
        const auto& previous = top20[i - 1];
        const auto& current = top20[i];
        EXPECT_TRUE(
            previous.second > current.second
            || (previous.second == current.second && previous.first <= current.first));
    }
}

TEST_F(TrainingRunnerTest, CommandOutcomeSignaturesUseDecisionAnchorWhenTreeMoves)
{
    config_.maxSimulationTime = 600.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    const std::string brainKind = "WaitOnlyForShiftedAnchorOutcome";
    TrainingBrainRegistry registry;
    registry.registerBrain(
        OrganismType::TREE,
        brainKind,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<TestTreeBrain>(nullptr);
                    return world.getOrganismManager().createTree(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;

    TrainingRunner::Config runnerConfig{ .brainRegistry = registry };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    const TrainingRunner::Status firstStatus = runner.step(1);
    ASSERT_EQ(firstStatus.state, TrainingRunner::State::Running);

    World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);
    const Organism::Body* organism = runner.getOrganism();
    ASSERT_NE(organism, nullptr);
    ASSERT_EQ(organism->getType(), OrganismType::TREE);
    Tree* tree = world->getOrganismManager().getTree(organism->getId());
    ASSERT_NE(tree, nullptr);

    const Vector2i initialAnchor = tree->getAnchorCell();
    tree->setCurrentCommand(GrowLeafCommand{
        .target_pos = {
            static_cast<int16_t>(initialAnchor.x + 1),
            static_cast<int16_t>(initialAnchor.y),
        },
        .execution_time_seconds = 0.0,
    });
    tree->setTimeRemaining(0.0);

    tree->setAnchorCell(Vector2i{ initialAnchor.x + 2, initialAnchor.y });

    const TrainingRunner::Status secondStatus = runner.step(1);
    ASSERT_EQ(secondStatus.state, TrainingRunner::State::Running);

    const auto outcomes = runner.getTopCommandOutcomeSignatures(20);
    ASSERT_FALSE(outcomes.empty());

    bool foundExpectedPrefix = false;
    bool foundExecutionAnchorPrefix = false;
    for (const auto& [signature, count] : outcomes) {
        if (signature.rfind("GrowLeaf(+1,+0) -> ", 0) == 0 && count == 1) {
            foundExpectedPrefix = true;
        }
        if (signature.rfind("GrowLeaf(-1,+0) -> ", 0) == 0 && count == 1) {
            foundExecutionAnchorPrefix = true;
        }
    }

    EXPECT_TRUE(foundExpectedPrefix);
    EXPECT_FALSE(foundExecutionAnchorPrefix);
}

TEST_F(TrainingRunnerTest, DuckTrainingPopulatesCommandSignatures)
{
    config_.maxSimulationTime = 5.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::Clock;
    spec.organismType = OrganismType::DUCK;

    std::vector<DuckInput> scriptedInputs;
    for (int i = 0; i < 6; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { 0.0f, 0.0f }, .jump = false });
    }
    for (int i = 0; i < 8; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { 1.0f, 0.0f }, .jump = false });
    }
    for (int i = 0; i < 8; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { -1.0f, 0.0f }, .jump = false });
    }
    for (int i = 0; i < 3; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { 0.0f, 0.0f }, .jump = true });
    }

    const std::string brainKind = "ScriptedDuckHistogram";
    TrainingBrainRegistry registry;
    registry.registerBrain(
        OrganismType::DUCK,
        brainKind,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [&scriptedInputs](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    return world.getOrganismManager().createDuck(
                        world, x, y, std::make_unique<ScriptedDuckBrain>(scriptedInputs));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;
    TrainingRunner::Config runnerConfig{ .brainRegistry = registry };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    const int stepsToRun = static_cast<int>(scriptedInputs.size()) + 12;
    for (int step = 0; step < stepsToRun; ++step) {
        const TrainingRunner::Status status = runner.step(1);
        ASSERT_EQ(status.state, TrainingRunner::State::Running);
    }

    const auto commands = runner.getTopCommandSignatures(20);
    const auto outcomes = runner.getTopCommandOutcomeSignatures(20);

    ASSERT_FALSE(commands.empty());
    ASSERT_FALSE(outcomes.empty());

    bool foundWait = false;
    bool foundRunLeft = false;
    bool foundRunRight = false;
    for (const auto& [signature, count] : commands) {
        if (signature == "Wait" && count > 0) {
            foundWait = true;
        }
        if (signature == "RunLeft" && count > 0) {
            foundRunLeft = true;
        }
        if (signature == "RunRight" && count > 0) {
            foundRunRight = true;
        }
    }

    EXPECT_TRUE(foundWait);
    EXPECT_TRUE(foundRunLeft);
    EXPECT_TRUE(foundRunRight);
}

TEST_F(TrainingRunnerTest, DuckTrainingOnClockDisablesClockDuckEvent)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::Clock;
    spec.organismType = OrganismType::DUCK;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::Random;
    individual.scenarioId = Scenario::EnumType::Clock;

    TrainingRunner runner(spec, individual, config_, genomeRepository_);

    const ScenarioConfig scenarioConfig = runner.getScenarioConfig();
    const auto* clockConfig = std::get_if<Config::Clock>(&scenarioConfig);
    ASSERT_NE(clockConfig, nullptr);
    EXPECT_FALSE(clockConfig->duckEnabled);
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
