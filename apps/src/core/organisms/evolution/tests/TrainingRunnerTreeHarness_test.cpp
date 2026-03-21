#include "core/LoggingChannels.h"
#include "core/World.h"
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
#include "core/organisms/evolution/TreeEvaluator.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace DirtSim {

class TrainingRunnerTest : public ::testing::Test {
protected:
    void SetUp() override { rng_.seed(std::random_device{}()); }

    std::mt19937 rng_;
    EvolutionConfig config_;
    GenomeRepository genomeRepository_;
};

namespace {

class ScopedChannelLevel {
public:
    ScopedChannelLevel(LogChannel channel, spdlog::level::level_enum level)
        : logger_(LoggingChannels::get(channel)),
          previousLevel_(logger_ ? logger_->level() : spdlog::level::off)
    {
        if (logger_) {
            logger_->set_level(level);
        }
    }

    ~ScopedChannelLevel()
    {
        if (logger_) {
            logger_->set_level(previousLevel_);
        }
    }

private:
    std::shared_ptr<spdlog::logger> logger_;
    spdlog::level::level_enum previousLevel_;
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
    size_t maxEntries = 5)
{
    std::unordered_map<std::string, int> countsByMessage;
    int total = 0;
    for (const auto& entry : commands) {
        if (entry.result.result != filter) {
            continue;
        }
        countsByMessage[entry.result.message]++;
        total++;
    }

    if (total == 0) {
        return;
    }

    std::vector<std::pair<std::string, int>> rolled;
    rolled.reserve(countsByMessage.size());
    for (const auto& entry : countsByMessage) {
        rolled.push_back(entry);
    }
    std::sort(rolled.begin(), rolled.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::cout << label << " (" << total << ")\n";
    const size_t limit = std::min(maxEntries, rolled.size());
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
        const std::string key = keyFunc(cmd);
        const auto it = indexByKey.find(key);
        if (it == indexByKey.end()) {
            indexByKey.emplace(key, rolled.size());
            rolled.push_back({ key, 1 });
            continue;
        }
        rolled[it->second].second++;
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
        const std::string key = formatCommand(flattened[i]) + " -> " + results[i];
        const auto it = indexByKey.find(key);
        if (it == indexByKey.end()) {
            indexByKey.emplace(key, rolled.size());
            rolled.push_back({ key, 1 });
            continue;
        }
        rolled[it->second].second++;
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
        + breakdown.structureBonus + breakdown.milestoneBonus + breakdown.commandScore
        + breakdown.seedScore;

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
    std::cout << "  - seed score: " << breakdown.seedScore << "\n";
    std::cout << "  - core term: " << coreTerm << "\n";
    std::cout << "  - bonus term: " << bonusTerm << "\n";

    std::cout.flags(oldFlags);
    std::cout.precision(oldPrecision);
}

} // namespace

TEST_F(TrainingRunnerTest, TreeScenarioBrainHarness)
{
    LoggingChannels::initialize();
    ScopedChannelLevel brainLogLevel(LogChannel::Brain, spdlog::level::off);
    ScopedChannelLevel treeLogLevel(LogChannel::Tree, spdlog::level::off);

    config_.maxSimulationTime = 600.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    struct BrainCase {
        std::string brainKind;
        std::optional<Genome> genome;
    };

    const std::vector<BrainCase> brains{
        { TrainingBrainKind::NeuralNet, NeuralNetBrain::randomGenome(rng_) },
        { TrainingBrainKind::NeuralNet, NeuralNetBrain::randomGenome(rng_) },
        { TrainingBrainKind::NeuralNet, NeuralNetBrain::randomGenome(rng_) },
        { TrainingBrainKind::RuleBased, std::nullopt },
        { TrainingBrainKind::RuleBased2, std::nullopt },
    };

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
                    .createRandomGenome =
                        [](std::mt19937& rng) { return NeuralNetBrain::randomGenome(rng); },
                    .isGenomeCompatible =
                        [](const Genome& genome) {
                            return NeuralNetBrain::isGenomeCompatible(genome);
                        },
                    .getGenomeLayout = []() { return NeuralNetBrain::getGenomeLayout(); },
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
                    .getGenomeLayout = nullptr,
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
                    .getGenomeLayout = nullptr,
                });
        }

        TrainingRunner::Individual individual;
        individual.brain.brainKind = brainCase.brainKind;
        individual.genome = brainCase.genome;

        const TrainingRunner::Config runnerConfig{
            .brainRegistry = registry,
            .duckClockSpawnLeftFirst = std::nullopt,
            .duckClockSpawnRngSeed = std::nullopt,
        };
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
            if (!current.has_value()) {
                lastStarted.reset();
                continue;
            }

            const std::string currentKey = formatCommand(*current);
            if (!lastStarted.has_value() || *lastStarted != currentKey) {
                started.push_back(*current);
                lastStarted = currentKey;
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

} // namespace DirtSim
