#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/TreeBrain.h"
#include "core/organisms/TreeCommandProcessor.h"
#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"
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
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/clock_scenario/DoorManager.h"
#include "core/scenarios/clock_scenario/ObstacleManager.h"
#include "core/scenarios/nes/NesGameAdapter.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

std::optional<std::filesystem::path> resolveNesFixtureRomPath()
{
    const std::filesystem::path repoRelativeRomPath =
        std::filesystem::path("testdata") / "roms" / "Flappy.Paratroopa.World.Unl.nes";
    if (std::filesystem::exists(repoRelativeRomPath)) {
        return repoRelativeRomPath;
    }

    if (const char* romPathEnv = std::getenv("DIRTSIM_NES_TEST_ROM_PATH"); romPathEnv != nullptr) {
        const std::filesystem::path romPath{ romPathEnv };
        if (!std::filesystem::exists(romPath)) {
            return std::nullopt;
        }

        std::filesystem::create_directories(repoRelativeRomPath.parent_path());
        std::error_code ec;
        std::filesystem::copy_file(
            romPath, repoRelativeRomPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec && std::filesystem::exists(repoRelativeRomPath)) {
            return repoRelativeRomPath;
        }
    }

    return std::nullopt;
}

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

// Brain that runs away from the entrance door, then reverses to exit through it.
class DoorExitDuckBrain : public DuckBrain {
public:
    explicit DoorExitDuckBrain(DoorSide entranceSide) : entranceSide_(entranceSide) {}

    void think(Duck& duck, const DuckSensoryData& /*sensory*/, double /*deltaTime*/) override
    {
        // Run away from entrance, or back toward it when told to return.
        float direction = (entranceSide_ == DoorSide::LEFT) ? 1.0f : -1.0f;
        if (returnToEntrance_) {
            direction = -direction;
        }
        duck.setInput(DuckInput{ .move = { direction, 0.0f }, .run = true });
    }

    void setReturnToEntrance() { returnToEntrance_ = true; }

private:
    DoorSide entranceSide_;
    bool returnToEntrance_ = false;
};

class RecordingNesAdapter : public NesGameAdapter {
public:
    RecordingNesAdapter(int* controllerCallCount, int* evaluateCallCount)
        : controllerCallCount_(controllerCallCount), evaluateCallCount_(evaluateCallCount)
    {}

    NesGameAdapterControllerOutput resolveControllerMask(
        const NesGameAdapterControllerInput& input) override
    {
        if (controllerCallCount_) {
            ++(*controllerCallCount_);
        }
        return NesGameAdapterControllerOutput{
            .resolvedControllerMask = input.inferredControllerMask,
        };
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        if (evaluateCallCount_) {
            ++(*evaluateCallCount_);
        }
        NesGameAdapterFrameOutput output;
        output.rewardDelta = static_cast<double>(input.advancedFrames);
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        DuckSensoryData sensory{};
        sensory.actual_width = DuckSensoryData::GRID_SIZE;
        sensory.actual_height = DuckSensoryData::GRID_SIZE;
        sensory.scale_factor = 1.0;
        sensory.world_offset = { 0, 0 };
        sensory.position = { DuckSensoryData::GRID_SIZE / 2, DuckSensoryData::GRID_SIZE / 2 };
        sensory.delta_time_seconds = input.deltaTimeSeconds;
        return sensory;
    }

private:
    int* controllerCallCount_ = nullptr;
    int* evaluateCallCount_ = nullptr;
};

class TraceNesAdapter : public NesGameAdapter {
public:
    NesGameAdapterControllerOutput resolveControllerMask(
        const NesGameAdapterControllerInput& /*input*/) override
    {
        return NesGameAdapterControllerOutput{
            .resolvedControllerMask = NesPolicyLayout::ButtonStart,
            .source = NesGameAdapterControllerSource::ScriptedSetup,
            .sourceFrameIndex = 42u,
        };
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        return NesGameAdapterFrameOutput{
            .debugState =
                NesGameAdapterDebugState{
                    .advancedFrameCount = input.advancedFrames,
                    .level = 3u,
                    .lifeState = 0u,
                    .lives = 4u,
                    .phase = 1u,
                    .playerXScreen = 120u,
                    .playerYScreen = 80u,
                    .powerupState = 2u,
                    .world = 2u,
                    .absoluteX = 512u,
                    .setupFailure = false,
                    .setupScriptActive = false,
                },
            .fitnessDetails = std::monostate{},
            .rewardDelta = 7.5,
            .gameState = 1u,
        };
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        DuckSensoryData sensory{};
        sensory.actual_width = DuckSensoryData::GRID_SIZE;
        sensory.actual_height = DuckSensoryData::GRID_SIZE;
        sensory.scale_factor = 1.0;
        sensory.world_offset = { 0, 0 };
        sensory.position = { DuckSensoryData::GRID_SIZE / 2, DuckSensoryData::GRID_SIZE / 2 };
        sensory.delta_time_seconds = input.deltaTimeSeconds;
        return sensory;
    }
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

// Proves the core design - that we can step incrementally without blocking.
TEST_F(TrainingRunnerTest, StepIsIncrementalNotBlocking)
{
    config_.maxSimulationTime = 1.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = NeuralNetBrain::randomGenome(rng_);

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

TEST_F(TrainingRunnerTest, TreeGerminationUsesScenarioInitialWorldSize)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = NeuralNetBrain::randomGenome(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);

    const World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->getData().width, 32);
    EXPECT_EQ(world->getData().height, 32);
}

TEST_F(TrainingRunnerTest, TrainingBrainRegistryIncludesNesScenarioDrivenEntry)
{
    TrainingBrainRegistry registry = TrainingBrainRegistry::createDefault();
    const BrainRegistryEntry* entry =
        registry.find(OrganismType::NES_DUCK, TrainingBrainKind::DuckNeuralNetRecurrentV2, "");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->controlMode, BrainRegistryEntry::ControlMode::ScenarioDriven);
    EXPECT_TRUE(entry->requiresGenome);
    ASSERT_TRUE(entry->createRandomGenome);
    ASSERT_TRUE(entry->isGenomeCompatible);

    const Genome genome = entry->createRandomGenome(rng_);
    EXPECT_TRUE(entry->isGenomeCompatible(genome));
}

TEST_F(TrainingRunnerTest, NesFlappyScenarioDrivenRunnerDoesNotSpawnOrganism)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    spec.organismType = OrganismType::NES_DUCK;

    TrainingBrainRegistry registry = TrainingBrainRegistry::createDefault();
    const BrainRegistryEntry* entry =
        registry.find(OrganismType::NES_DUCK, TrainingBrainKind::DuckNeuralNetRecurrentV2, "");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->createRandomGenome);

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    individual.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    individual.genome = entry->createRandomGenome(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);
    const auto status = runner.step(0);

    EXPECT_EQ(runner.getOrganism(), nullptr);
    EXPECT_EQ(status.nesFramesSurvived, 0u);
    EXPECT_DOUBLE_EQ(status.nesRewardTotal, 0.0);
}

TEST_F(TrainingRunnerTest, NesFlappyScenarioDrivenRunnerTerminatesBeforeInfiniteLoop)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    config_.maxSimulationTime = 60.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    spec.organismType = OrganismType::NES_DUCK;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    individual.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    individual.genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng_);
    std::fill(individual.genome->weights.begin(), individual.genome->weights.end(), 0.0f);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);

    TrainingRunner::Status status;
    int steps = 0;
    while ((status = runner.step(1)).state == TrainingRunner::State::Running) {
        steps++;
        ASSERT_LT(steps, 10000) << "NES runner should terminate within a reasonable frame budget";
    }

    EXPECT_NE(status.state, TrainingRunner::State::Running);
    if (status.state == TrainingRunner::State::OrganismDied) {
        EXPECT_LT(status.simTime, config_.maxSimulationTime);
    }
    else {
        EXPECT_EQ(status.state, TrainingRunner::State::TimeExpired);
        EXPECT_GE(status.simTime, config_.maxSimulationTime);
    }
    EXPECT_GT(status.nesFramesSurvived, 0u);

    const auto commands = runner.getTopCommandSignatures(20);
    const auto outcomes = runner.getTopCommandOutcomeSignatures(20);
    EXPECT_FALSE(commands.empty());
    EXPECT_FALSE(outcomes.empty());

    bool sawFlap = false;
    bool sawStart = false;
    for (const auto& [signature, count] : commands) {
        if (count <= 0) {
            continue;
        }
        if (signature.find("Flap") != std::string::npos) {
            sawFlap = true;
        }
        if (signature.find("Start") != std::string::npos) {
            sawStart = true;
        }
    }

    EXPECT_TRUE(sawFlap || sawStart);
}

TEST_F(TrainingRunnerTest, NesScenarioDrivenRunnerUsesConfiguredNesGameAdapterRegistry)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    config_.maxSimulationTime = 2.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    spec.organismType = OrganismType::NES_DUCK;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    individual.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    individual.genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng_);

    int controllerCalls = 0;
    int evaluateCalls = 0;
    NesGameAdapterRegistry adapterRegistry;
    adapterRegistry.registerAdapter(
        Scenario::EnumType::NesFlappyParatroopa, [&controllerCalls, &evaluateCalls]() {
            return std::make_unique<RecordingNesAdapter>(&controllerCalls, &evaluateCalls);
        });

    TrainingRunner::Config runnerConfig{
        .brainRegistry = TrainingBrainRegistry::createDefault(),
        .nesGameAdapterRegistry = adapterRegistry,
        .duckClockSpawnLeftFirst = std::nullopt,
        .duckClockSpawnRngSeed = std::nullopt,
    };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    TrainingRunner::Status status;
    int steps = 0;
    while ((status = runner.step(1)).state == TrainingRunner::State::Running) {
        steps++;
        ASSERT_LT(steps, 300) << "Runner should complete this short session";
    }

    EXPECT_GT(controllerCalls, 0);
    EXPECT_GT(evaluateCalls, 0);
    EXPECT_GT(status.nesFramesSurvived, 0u);
}

TEST_F(TrainingRunnerTest, NesScenarioDrivenRunnerEmitsPerFrameTrace)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    config_.maxSimulationTime = 1.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    spec.organismType = OrganismType::NES_DUCK;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    individual.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    individual.genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng_);

    std::vector<TrainingRunner::FrameTrace> traces;
    NesGameAdapterRegistry adapterRegistry;
    adapterRegistry.registerAdapter(Scenario::EnumType::NesFlappyParatroopa, []() {
        return std::make_unique<TraceNesAdapter>();
    });

    TrainingRunner::Config runnerConfig{
        .brainRegistry = TrainingBrainRegistry::createDefault(),
        .nesGameAdapterRegistry = adapterRegistry,
        .duckClockSpawnLeftFirst = std::nullopt,
        .duckClockSpawnRngSeed = std::nullopt,
        .frameTraceSink =
            [&traces](const TrainingRunner::FrameTrace& trace) { traces.push_back(trace); },
    };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    const TrainingRunner::Status status = runner.step(3);

    ASSERT_FALSE(traces.empty());
    EXPECT_EQ(traces.front().stepOrdinal, 1u);
    EXPECT_GT(traces.front().simTime, 0.0);
    ASSERT_TRUE(traces.front().nes.has_value());
    EXPECT_TRUE(traces.front().nes->frameAdvanced);
    EXPECT_EQ(traces.front().nes->resolvedControllerMask, NesPolicyLayout::ButtonStart);
    EXPECT_EQ(
        traces.front().nes->controllerSource,
        std::optional<NesGameAdapterControllerSource>(
            NesGameAdapterControllerSource::ScriptedSetup));
    EXPECT_EQ(traces.front().nes->controllerSourceFrameIndex, std::optional<uint64_t>(42u));
    EXPECT_EQ(traces.front().nes->commandSignature, "Start");
    EXPECT_EQ(traces.front().nes->commandOutcome, "FrameAdvanced");
    EXPECT_DOUBLE_EQ(traces.front().nes->rewardDelta, 7.5);
    EXPECT_EQ(traces.front().nes->lastGameStateBefore, std::nullopt);
    EXPECT_EQ(traces.front().nes->lastGameStateAfter, std::optional<uint8_t>(1u));
    ASSERT_TRUE(traces.front().nes->debugState.has_value());
    EXPECT_EQ(traces.front().nes->debugState->world, std::optional<uint8_t>(2u));
    EXPECT_EQ(traces.front().nes->debugState->level, std::optional<uint8_t>(3u));
    ASSERT_TRUE(runner.getNesLastControllerTelemetry().has_value());
    const NesControllerTelemetry& telemetry = runner.getNesLastControllerTelemetry().value();
    EXPECT_EQ(telemetry.resolvedControllerMask, NesPolicyLayout::ButtonStart);
    EXPECT_EQ(telemetry.controllerSource, NesGameAdapterControllerSource::ScriptedSetup);
    EXPECT_EQ(telemetry.controllerSourceFrameIndex, std::optional<uint64_t>(42u));
    EXPECT_TRUE(std::isfinite(telemetry.xRaw));
    EXPECT_TRUE(std::isfinite(telemetry.yRaw));
    EXPECT_TRUE(std::isfinite(telemetry.aRaw));
    EXPECT_TRUE(std::isfinite(telemetry.bRaw));
    EXPECT_EQ(status.state, TrainingRunner::State::Running);
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
    individual.genome = NeuralNetBrain::randomGenome(rng_);

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
            .getGenomeLayout = nullptr,
        });

    TrainingRunner::Config runnerConfig{
        .brainRegistry = registry,
        .duckClockSpawnLeftFirst = std::nullopt,
        .duckClockSpawnRngSeed = std::nullopt,
    };
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
    brains.push_back({ TrainingBrainKind::NeuralNet, NeuralNetBrain::randomGenome(rng_) });
    brains.push_back({ TrainingBrainKind::NeuralNet, NeuralNetBrain::randomGenome(rng_) });
    brains.push_back({ TrainingBrainKind::NeuralNet, NeuralNetBrain::randomGenome(rng_) });
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

        TrainingRunner::Config runnerConfig{
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
            .getGenomeLayout = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;

    TrainingRunner::Config runnerConfig{
        .brainRegistry = registry,
        .duckClockSpawnLeftFirst = std::nullopt,
        .duckClockSpawnRngSeed = std::nullopt,
    };
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
            .getGenomeLayout = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;

    TrainingRunner::Config runnerConfig{
        .brainRegistry = registry,
        .duckClockSpawnLeftFirst = std::nullopt,
        .duckClockSpawnRngSeed = std::nullopt,
    };
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

TEST_F(TrainingRunnerTest, ClockDuckPopulatesCommandSignatures)
{
    config_.maxSimulationTime = 5.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::Clock;
    spec.organismType = OrganismType::DUCK;

    std::vector<DuckInput> scriptedInputs;
    for (int i = 0; i < 6; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { 0.0f, 0.0f }, .jump = false });
    }
    for (int i = 0; i < 6; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { 0.2f, 0.0f }, .jump = false });
    }
    for (int i = 0; i < 6; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { -0.2f, 0.0f }, .jump = false });
    }
    for (int i = 0; i < 8; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { 1.0f, 0.0f }, .jump = false, .run = true });
    }
    for (int i = 0; i < 8; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { -1.0f, 0.0f }, .jump = false, .run = true });
    }
    for (int i = 0; i < 3; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { -1.0f, 0.0f }, .jump = true });
    }
    for (int i = 0; i < 3; ++i) {
        scriptedInputs.push_back(DuckInput{ .move = { 1.0f, 0.0f }, .jump = true });
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
            .getGenomeLayout = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;
    TrainingRunner::Config runnerConfig{
        .brainRegistry = registry,
        .duckClockSpawnLeftFirst = std::nullopt,
        .duckClockSpawnRngSeed = std::nullopt,
    };
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
    bool foundWalkLeft = false;
    bool foundWalkRight = false;
    bool foundRunLeft = false;
    bool foundRunRight = false;
    bool foundJumpLeft = false;
    bool foundJumpRight = false;
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
        if (signature == "WalkLeft" && count > 0) {
            foundWalkLeft = true;
        }
        if (signature == "WalkRight" && count > 0) {
            foundWalkRight = true;
        }
        if (signature == "JumpLeft" && count > 0) {
            foundJumpLeft = true;
        }
        if (signature == "JumpRight" && count > 0) {
            foundJumpRight = true;
        }
    }

    EXPECT_TRUE(foundWait);
    EXPECT_TRUE(foundWalkLeft);
    EXPECT_TRUE(foundWalkRight);
    EXPECT_TRUE(foundRunLeft);
    EXPECT_TRUE(foundRunRight);
    EXPECT_TRUE(foundJumpLeft);
    EXPECT_TRUE(foundJumpRight);
}

TEST_F(TrainingRunnerTest, ClockDuckDisablesClockDuckEvent)
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
    EXPECT_TRUE(clockConfig->meltdownEnabled);
    EXPECT_FALSE(clockConfig->obstacleCourseEnabled);
    EXPECT_TRUE(clockConfig->rainEnabled);
}

TEST_F(TrainingRunnerTest, ClockDuckRandomizesSpawnSide)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::Clock;
    spec.organismType = OrganismType::DUCK;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::Random;
    individual.scenarioId = Scenario::EnumType::Clock;

    bool sawLeftSpawn = false;
    bool sawRightSpawn = false;
    for (uint32_t seed = 0; seed < 64; ++seed) {
        const TrainingRunner::Config runnerConfig{
            .brainRegistry = TrainingBrainRegistry::createDefault(),
            .duckClockSpawnLeftFirst = std::nullopt,
            .duckClockSpawnRngSeed = seed,
        };
        TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

        World* world = runner.getWorld();
        if (!world) {
            ADD_FAILURE() << "World must exist for duck spawn test";
            continue;
        }

        auto& data = world->getData();
        const int leftX = 0;
        const int rightX = std::max(0, data.width - 1);

        runner.step(0);
        const Organism::Body* organism = runner.getOrganism();
        if (!organism) {
            ADD_FAILURE() << "Duck must spawn during first step";
            continue;
        }

        const int spawnX = organism->getAnchorCell().x;
        if (spawnX == leftX) {
            sawLeftSpawn = true;
        }
        else if (spawnX == rightX) {
            sawRightSpawn = true;
        }
        else {
            ADD_FAILURE() << "Duck spawned outside expected clock door cells";
        }
    }

    EXPECT_TRUE(sawLeftSpawn);
    EXPECT_TRUE(sawRightSpawn);
}

TEST_F(TrainingRunnerTest, ClockDuckSpawnSideOverrideRespectsRequestedSide)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::Clock;
    spec.organismType = OrganismType::DUCK;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::Random;
    individual.scenarioId = Scenario::EnumType::Clock;

    const auto spawnWithOverride = [&](bool spawnLeftFirst) -> int {
        const TrainingRunner::Config runnerConfig{
            .brainRegistry = TrainingBrainRegistry::createDefault(),
            .duckClockSpawnLeftFirst = spawnLeftFirst,
            .duckClockSpawnRngSeed = std::nullopt,
        };
        TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

        World* world = runner.getWorld();
        if (!world) {
            ADD_FAILURE() << "World must exist for duck spawn test";
            return -1;
        }

        runner.step(0);
        const Organism::Body* organism = runner.getOrganism();
        if (!organism) {
            ADD_FAILURE() << "Duck must spawn during first step";
            return -1;
        }

        return organism->getAnchorCell().x;
    };

    const int leftSpawnX = spawnWithOverride(true);
    const int rightSpawnX = spawnWithOverride(false);

    EXPECT_EQ(leftSpawnX, 0);
    EXPECT_NE(rightSpawnX, leftSpawnX);
}

TEST_F(TrainingRunnerTest, ClockDuckDoorLifecycle)
{
    // Short sim so exitDoorOpenTime = max(0, 12 - 10) = 2.0 seconds.
    config_.maxSimulationTime = 12.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::Clock;
    spec.organismType = OrganismType::DUCK;

    // We need a raw pointer to flip the brain's direction mid-test.
    DoorExitDuckBrain* brainPtr = nullptr;

    const std::string brainKind = "DoorExitBrain";
    TrainingBrainRegistry registry;
    registry.registerBrain(
        OrganismType::DUCK,
        brainKind,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [&brainPtr](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    auto brain = std::make_unique<DoorExitDuckBrain>(DoorSide::LEFT);
                    brainPtr = brain.get();
                    return world.getOrganismManager().createDuck(world, x, y, std::move(brain));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;
    individual.scenarioId = Scenario::EnumType::Clock;
    const TrainingRunner::Config runnerConfig{
        .brainRegistry = registry,
        .duckClockSpawnLeftFirst = true,
        .duckClockSpawnRngSeed = std::nullopt,
    };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);
    const WorldData& data = world->getData();

    // Expected door position: left wall, one cell above the floor wall.
    const Vector2i doorPos{ 0, data.height - 2 };
    const Vector2i roofPos{ 1, data.height - 3 };

    // Access the DoorManager through the ClockScenario.
    auto* clockScenario = dynamic_cast<ClockScenario*>(world->getScenario());
    ASSERT_NE(clockScenario, nullptr);
    DoorManager& doorMgr = clockScenario->getDoorManager();

    // ----------------------------------------------------------------
    // Phase 1: Spawn — duck should be at the open door cell.
    // ----------------------------------------------------------------
    runner.step(0);
    const Organism::Body* organism = runner.getOrganism();
    ASSERT_NE(organism, nullptr) << "Duck must spawn during first step";
    ASSERT_NE(brainPtr, nullptr) << "Brain must be created during spawn";

    EXPECT_EQ(organism->getAnchorCell(), doorPos) << "Duck should spawn at the open door cell";
    // The door cell is not AIR because the duck now occupies it. Verify the
    // door is logically open and the cell is not WALL (i.e. the wall was removed).
    EXPECT_NE(data.at(doorPos.x, doorPos.y).material_type, Material::EnumType::Wall)
        << "Door cell should not be WALL (door should be open)";
    EXPECT_TRUE(doorMgr.isOpenDoorAt(doorPos, data))
        << "DoorManager should report door as open at door position";
    EXPECT_EQ(data.at(roofPos.x, roofPos.y).material_type, Material::EnumType::Wall)
        << "Roof cell should be WALL when door is open";

    // ----------------------------------------------------------------
    // Phase 2: Step until entrance door closes (duck moves away).
    // ----------------------------------------------------------------
    bool entranceClosed = false;
    for (int i = 0; i < 500; ++i) {
        runner.step(1);
        organism = runner.getOrganism();
        ASSERT_NE(organism, nullptr) << "Duck died before entrance closed (step " << i << ")";

        if (organism->getAnchorCell() != doorPos) {
            // Duck has left the door cell. The door should close this frame or next.
            runner.step(1);
            if (!data.at(doorPos.x, doorPos.y).isAir()) {
                entranceClosed = true;
                break;
            }
        }
    }
    ASSERT_TRUE(entranceClosed) << "Entrance door never closed";
    EXPECT_EQ(data.at(doorPos.x, doorPos.y).material_type, Material::EnumType::Wall)
        << "Entrance door cell should be WALL after closing";
    EXPECT_TRUE(data.at(roofPos.x, roofPos.y).isAir())
        << "Roof cell should be AIR after door closes";

    // ----------------------------------------------------------------
    // Phase 2b: Run duck past the world midpoint (required for exit door).
    // ----------------------------------------------------------------
    const int midX = data.width / 2;
    bool reachedMiddle = false;
    for (int i = 0; i < 2000; ++i) {
        const TrainingRunner::Status status = runner.step(1);
        if (status.state != TrainingRunner::State::Running) {
            break;
        }
        organism = runner.getOrganism();
        ASSERT_NE(organism, nullptr) << "Duck died before reaching middle (step " << i << ")";
        if (organism->getAnchorCell().x >= midX) {
            reachedMiddle = true;
            break;
        }
    }
    ASSERT_TRUE(reachedMiddle) << "Duck never reached the world midpoint";

    // Reverse the duck so it heads back toward the entrance (now exit).
    brainPtr->setReturnToEntrance();

    // ----------------------------------------------------------------
    // Phase 3: Step until exit door reopens (simTime >= 2.0).
    // ----------------------------------------------------------------
    const double exitDoorOpenTime = std::max(0.0, config_.maxSimulationTime - 10.0);
    bool exitDoorOpened = false;
    for (int i = 0; i < 2000; ++i) {
        const TrainingRunner::Status status = runner.step(1);
        if (status.state != TrainingRunner::State::Running) {
            break;
        }
        if (runner.getSimTime() >= exitDoorOpenTime && data.at(doorPos.x, doorPos.y).isAir()) {
            exitDoorOpened = true;
            break;
        }
    }
    ASSERT_TRUE(exitDoorOpened) << "Exit door never reopened (simTime=" << runner.getSimTime()
                                << ")";

    // ----------------------------------------------------------------
    // Phase 4: Step until duck exits through the door.
    // ----------------------------------------------------------------
    bool exitedThroughDoor = false;
    for (int i = 0; i < 2000; ++i) {
        const TrainingRunner::Status status = runner.step(1);
        if (status.exitedThroughDoor) {
            exitedThroughDoor = true;
            break;
        }
        if (status.state != TrainingRunner::State::Running) {
            break;
        }
    }
    ASSERT_TRUE(exitedThroughDoor) << "Duck never exited through the door";
    EXPECT_EQ(runner.getOrganism(), nullptr) << "Duck should be removed after exiting";

    const TrainingRunner::Status finalStatus = runner.getStatus();
    EXPECT_TRUE(finalStatus.exitedThroughDoor);
    EXPECT_GT(finalStatus.exitDoorTime, 0.0);
}

TEST_F(TrainingRunnerTest, SpawnPrefersNearestAirInTopHalf)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = NeuralNetBrain::randomGenome(rng_);

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
    individual.genome = NeuralNetBrain::randomGenome(rng_);

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

TEST_F(TrainingRunnerTest, ClockDuckPitDamageKillsDuck)
{
    config_.maxSimulationTime = 10.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::Clock;
    spec.organismType = OrganismType::DUCK;

    // Brain that does nothing — duck stands still.
    const std::string brainKind = "IdleDuckBrain";
    TrainingBrainRegistry registry;
    registry.registerBrain(
        OrganismType::DUCK,
        brainKind,
        "",
        BrainRegistryEntry{
            .requiresGenome = false,
            .allowsMutation = false,
            .spawn =
                [](World& world, uint32_t x, uint32_t y, const Genome* /*genome*/) {
                    return world.getOrganismManager().createDuck(
                        world, x, y, std::make_unique<ScriptedDuckBrain>(std::vector<DuckInput>{}));
                },
            .createRandomGenome = nullptr,
            .isGenomeCompatible = nullptr,
            .getGenomeLayout = nullptr,
        });

    TrainingRunner::Individual individual;
    individual.brain.brainKind = brainKind;
    individual.scenarioId = Scenario::EnumType::Clock;
    const TrainingRunner::Config runnerConfig{
        .brainRegistry = registry,
        .duckClockSpawnLeftFirst = true,
        .duckClockSpawnRngSeed = std::nullopt,
    };
    TrainingRunner runner(spec, individual, config_, genomeRepository_, runnerConfig);

    World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);

    // Enable obstacle course so the scenario doesn't clear our pit.
    auto* clockScenario = dynamic_cast<ClockScenario*>(world->getScenario());
    ASSERT_NE(clockScenario, nullptr);
    ScenarioConfig scenarioConfig = clockScenario->getConfig();
    auto* clockConfig = std::get_if<Config::Clock>(&scenarioConfig);
    ASSERT_NE(clockConfig, nullptr);
    clockConfig->obstacleCourseEnabled = true;
    clockScenario->setConfig(scenarioConfig, *world);

    // Spawn the duck.
    runner.step(0);
    const Organism::Body* organism = runner.getOrganism();
    ASSERT_NE(organism, nullptr) << "Duck must spawn during first step";

    // Let the duck settle onto the floor.
    for (int i = 0; i < 30; ++i) {
        runner.step(1);
        organism = runner.getOrganism();
        ASSERT_NE(organism, nullptr) << "Duck died before pit was created";
    }

    // Create a wide pit under and around the duck so it falls in.
    const Vector2i duckCell = organism->getAnchorCell();
    WorldData& data = world->getData();
    const int floorY = data.height - 1;
    const int pitStart = std::max(0, duckCell.x - 2);
    const int pitEnd = std::min(static_cast<int>(data.width) - 1, duckCell.x + 2);
    const int pitWidth = pitEnd - pitStart + 1;
    clockScenario->obstacle_manager.addObstacle(
        FloorObstacle{
            .start_x = pitStart,
            .width = pitWidth,
            .type = FloorObstacleType::PIT,
        });
    for (int x = pitStart; x <= pitEnd; ++x) {
        data.at(x, floorY).clear();
    }

    // Step until the duck dies from pit damage. At 1.0 hp/sec with regen at 0.10/sec,
    // net damage is 0.90/sec, so death takes ~1.1 seconds (~67 frames at 1/60 timestep).
    bool duckDied = false;
    for (int i = 0; i < 200; ++i) {
        const TrainingRunner::Status status = runner.step(1);
        if (status.state == TrainingRunner::State::OrganismDied) {
            duckDied = true;
            break;
        }
    }
    EXPECT_TRUE(duckDied) << "Duck should die from standing in a pit";
    EXPECT_EQ(runner.getOrganism(), nullptr) << "Dead duck should be removed";
}

TEST_F(TrainingRunnerTest, NeuralNetTreeGrowthReplication)
{
    // Verify that command-specific action masking forces all action brains to pick
    // commands that pass the runtime validator.
    constexpr int kTrials = 5;

    int successes = 0;
    int wait_brains = 0;
    int cancel_brains = 0;
    int action_brains = 0;
    std::map<std::string, int> rejection_reasons;

    for (int trial = 0; trial < kTrials; ++trial) {
        const Genome genome = NeuralNetBrain::randomGenome(rng_);

        // Create world matching TreeGerminationScenario.
        auto world = std::make_unique<World>(9, 9);
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 9; ++x) {
                world->getData().at(x, y) = Cell();
            }
        }
        for (int y = 6; y < 9; ++y) {
            for (int x = 0; x < 9; ++x) {
                world->addMaterialAtCell(
                    { static_cast<int16_t>(x), static_cast<int16_t>(y) },
                    Material::EnumType::Dirt,
                    1.0);
            }
        }

        OrganismId id = world->getOrganismManager().createTree(*world, 4, 4);
        Tree* tree = world->getOrganismManager().getTree(id);
        tree->setBrain(std::make_unique<NeuralNetBrain>(genome));

        // Let seed fall and settle.
        for (int i = 0; i < 200; ++i) {
            world->advanceTime(0.016);
        }
        tree = world->getOrganismManager().getTree(id);
        if (!tree) continue;

        // Ask the brain for one decision and inspect it.
        const TreeSensoryData sensory = tree->gatherSensoryData(*world);
        NeuralNetBrain brain(genome);
        const TreeCommand cmd = brain.decide(sensory);

        std::string cmd_type;
        Vector2i target{ -1, -1 };
        std::visit(
            [&](auto&& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, WaitCommand>) {
                    cmd_type = "Wait";
                }
                else if constexpr (std::is_same_v<T, CancelCommand>) {
                    cmd_type = "Cancel";
                }
                else if constexpr (std::is_same_v<T, GrowWoodCommand>) {
                    cmd_type = "GrowWood";
                    target = c.target_pos;
                }
                else if constexpr (std::is_same_v<T, GrowLeafCommand>) {
                    cmd_type = "GrowLeaf";
                    target = c.target_pos;
                }
                else if constexpr (std::is_same_v<T, GrowRootCommand>) {
                    cmd_type = "GrowRoot";
                    target = c.target_pos;
                }
                else if constexpr (std::is_same_v<T, ProduceSeedCommand>) {
                    cmd_type = "ProduceSeed";
                    target = c.position;
                }
            },
            cmd);

        if (cmd_type == "Wait") {
            wait_brains++;
            continue;
        }
        if (cmd_type == "Cancel") {
            if (sensory.current_action.has_value()) {
                cancel_brains++;
            }
            else {
                rejection_reasons["cancel_while_idle"]++;
            }
            continue;
        }

        action_brains++;

        TreeCommandProcessor processor;
        const CommandExecutionResult validation = processor.validate(*tree, *world, cmd);
        if (validation.succeeded()) {
            successes++;
            const Vector2i seed_pos = tree->getAnchorCell();
            std::cout << "Trial " << trial << " *** VALID *** " << cmd_type << " at (" << target.x
                      << "," << target.y << ") seed=(" << seed_pos.x << "," << seed_pos.y << ")\n";
        }
        else {
            rejection_reasons[validation.message]++;

            if (trial < 10) {
                std::cout << "Trial " << trial << " " << cmd_type << " at (" << target.x << ","
                          << target.y << ") validation=\"" << validation.message << "\"\n";
            }
        }
    }

    std::cout << "\n=== Position diagnostic (" << kTrials << " brains) ===\n";
    std::cout << "Wait brains:       " << wait_brains << " (" << 100.0 * wait_brains / kTrials
              << "%)\n";
    std::cout << "Cancel brains:     " << cancel_brains << " (" << 100.0 * cancel_brains / kTrials
              << "%)\n";
    std::cout << "Action brains:     " << action_brains << " (" << 100.0 * action_brains / kTrials
              << "%)\n";
    std::cout << "Successes:         " << successes << "/" << kTrials << "\n";
    std::cout << "\nRejection reasons:\n";
    for (const auto& [reason, count] : rejection_reasons) {
        std::cout << "  " << reason << ": " << count << "\n";
    }

    EXPECT_EQ(rejection_reasons["cancel_while_idle"], 0) << "Cancel should be masked while idle.";

    // With command-specific masking, every action brain should pass runtime validation.
    if (action_brains > 0) {
        EXPECT_EQ(successes, action_brains)
            << "Expected all action brains to pass validation with command-specific masking.";
    }
}

} // namespace DirtSim
