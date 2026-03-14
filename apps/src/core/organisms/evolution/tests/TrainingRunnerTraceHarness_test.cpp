#include "core/ScenarioConfig.h"
#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingRunner.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/scenarios/tests/NesTestRomPath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace DirtSim {

namespace {

constexpr uint32_t kSmbHarnessGenomeSeed = 12345u;
constexpr double kSmbHarnessDefaultMaxSimulationTime = 12.0;
constexpr int kSmbHarnessRunCount = 3;
constexpr const char* kSmbHarnessGenomeDbPathEnv = "DIRTSIM_TRAINING_TRACE_GENOME_DB_PATH";
constexpr const char* kSmbHarnessGenomeIdEnv = "DIRTSIM_TRAINING_TRACE_GENOME_ID";
constexpr const char* kSmbHarnessMaxSimulationTimeEnv =
    "DIRTSIM_TRAINING_TRACE_MAX_SIMULATION_TIME";

struct SmbTraceHarnessRun {
    TrainingRunner::Status status;
    std::optional<NesSuperMarioBrosFitnessSnapshot> smbSnapshot = std::nullopt;
    std::vector<TrainingRunner::FrameTrace> traces;
};

struct GenomeSelection {
    TrainingRunner::BrainSpec brain;
    Genome genome;
    std::optional<GenomeId> genomeId = std::nullopt;
    std::optional<std::filesystem::path> repositoryPath = std::nullopt;
    std::string sourceDescription;
};

struct GenomeSelectionResult {
    std::optional<GenomeSelection> selection = std::nullopt;
    std::string error;
};

struct TraceDivergence {
    size_t traceIndex = 0;
    uint64_t stepOrdinal = 0;
    std::string baselineRow;
    std::string candidateRow;
    std::string reason;
};

TrainingSpec makeSmbHarnessTrainingSpec()
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::NesSuperMarioBros;
    spec.organismType = OrganismType::NES_DUCK;
    return spec;
}

Config::NesSuperMarioBros makeSmbHarnessScenarioConfig(const std::filesystem::path& romPath)
{
    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romPath = romPath.string();
    config.requireSmolnesMapper = true;
    return config;
}

Genome makeSmbHarnessGenome()
{
    std::mt19937 rng(kSmbHarnessGenomeSeed);
    return DuckNeuralNetRecurrentBrainV2::randomGenome(rng);
}

std::optional<std::string> getEnvValue(const char* envVarName)
{
    if (const char* value = std::getenv(envVarName); value != nullptr && value[0] != '\0') {
        return std::string(value);
    }

    return std::nullopt;
}

bool isSupportedSmbHarnessBrainKind(const std::string& brainKind)
{
    return brainKind == TrainingBrainKind::DuckNeuralNetRecurrentV2;
}

std::string formatBrainSpec(const TrainingRunner::BrainSpec& brain)
{
    if (!brain.brainVariant.has_value() || brain.brainVariant->empty()) {
        return brain.brainKind;
    }

    return brain.brainKind + ":" + brain.brainVariant.value();
}

GenomeSelection makeSeededSmbHarnessGenomeSelection()
{
    return GenomeSelection{
        .brain =
            TrainingRunner::BrainSpec{
                .brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2,
                .brainVariant = std::nullopt,
            },
        .genome = makeSmbHarnessGenome(),
        .sourceDescription = "seeded-random-genome",
    };
}

GenomeSelectionResult resolveConfiguredSmbHarnessGenomeSelection()
{
    const std::optional<std::string> genomeDbPath = getEnvValue(kSmbHarnessGenomeDbPathEnv);
    const std::optional<std::string> genomeIdString = getEnvValue(kSmbHarnessGenomeIdEnv);
    if (!genomeDbPath.has_value() && !genomeIdString.has_value()) {
        return GenomeSelectionResult{
            .selection = makeSeededSmbHarnessGenomeSelection(),
            .error = "",
        };
    }

    if (!genomeDbPath.has_value() || !genomeIdString.has_value()) {
        return GenomeSelectionResult{
            .error = "Both DIRTSIM_TRAINING_TRACE_GENOME_DB_PATH and "
                     "DIRTSIM_TRAINING_TRACE_GENOME_ID must be set together.",
        };
    }

    GenomeId genomeId = INVALID_GENOME_ID;
    try {
        genomeId = UUID::fromString(genomeIdString.value());
    }
    catch (const std::exception& e) {
        return GenomeSelectionResult{
            .error = "Failed to parse DIRTSIM_TRAINING_TRACE_GENOME_ID: " + std::string(e.what()),
        };
    }

    if (genomeId == INVALID_GENOME_ID) {
        return GenomeSelectionResult{
            .error = "DIRTSIM_TRAINING_TRACE_GENOME_ID resolved to a nil UUID.",
        };
    }

    const std::filesystem::path repositoryPath{ genomeDbPath.value() };
    GenomeRepository repository(repositoryPath);
    const std::optional<Genome> genome = repository.get(genomeId);
    if (!genome.has_value()) {
        return GenomeSelectionResult{
            .error = "Genome not found in repository: " + genomeId.toString(),
        };
    }

    const std::optional<GenomeMetadata> metadata = repository.getMetadata(genomeId);
    if (!metadata.has_value()) {
        return GenomeSelectionResult{
            .error = "Genome metadata missing in repository: " + genomeId.toString(),
        };
    }

    if (metadata->scenarioId != Scenario::EnumType::NesSuperMarioBros) {
        return GenomeSelectionResult{
            .error = "Configured genome is not tagged for NES Super Mario Bros.",
        };
    }

    if (metadata->organismType.has_value() && metadata->organismType != OrganismType::NES_DUCK) {
        return GenomeSelectionResult{
            .error = "Configured genome is not tagged as an NES duck organism.",
        };
    }

    const std::string brainKind =
        metadata->brainKind.value_or(TrainingBrainKind::DuckNeuralNetRecurrentV2);
    if (!isSupportedSmbHarnessBrainKind(brainKind)) {
        return GenomeSelectionResult{
            .error =
                "Configured genome brain kind is not supported by the SMB harness: " + brainKind,
        };
    }

    return GenomeSelectionResult{
        .selection =
            GenomeSelection{
                .brain =
                    TrainingRunner::BrainSpec{
                        .brainKind = brainKind,
                        .brainVariant = metadata->brainVariant,
                    },
                .genome = genome.value(),
                .genomeId = genomeId,
                .repositoryPath = repositoryPath,
                .sourceDescription = "repository-genome",
            },
        .error = "",
    };
}

struct MaxSimulationTimeResult {
    double value = kSmbHarnessDefaultMaxSimulationTime;
    std::string error;
};

MaxSimulationTimeResult resolveConfiguredSmbHarnessMaxSimulationTime()
{
    const std::optional<std::string> configuredValue = getEnvValue(kSmbHarnessMaxSimulationTimeEnv);
    if (!configuredValue.has_value()) {
        return MaxSimulationTimeResult{};
    }

    char* parseEnd = nullptr;
    const double parsedValue = std::strtod(configuredValue->c_str(), &parseEnd);
    if (parseEnd == configuredValue->c_str() || *parseEnd != '\0' || !std::isfinite(parsedValue)
        || parsedValue <= 0.0) {
        return MaxSimulationTimeResult{
            .error = "DIRTSIM_TRAINING_TRACE_MAX_SIMULATION_TIME must be a positive finite "
                     "number.",
        };
    }

    return MaxSimulationTimeResult{
        .value = parsedValue,
        .error = "",
    };
}

std::string formatBool(bool value)
{
    return value ? "1" : "0";
}

std::string formatOptionalBool(const std::optional<bool>& value)
{
    if (!value.has_value()) {
        return "";
    }
    return value.value() ? "1" : "0";
}

std::string formatOptionalUint8(const std::optional<uint8_t>& value)
{
    if (!value.has_value()) {
        return "";
    }
    return std::to_string(static_cast<unsigned int>(value.value()));
}

std::string formatOptionalUint16(const std::optional<uint16_t>& value)
{
    if (!value.has_value()) {
        return "";
    }
    return std::to_string(static_cast<unsigned int>(value.value()));
}

std::string formatOptionalUint64(const std::optional<uint64_t>& value)
{
    if (!value.has_value()) {
        return "";
    }
    return std::to_string(static_cast<unsigned long long>(value.value()));
}

std::string formatControllerSource(const std::optional<NesGameAdapterControllerSource>& source)
{
    if (!source.has_value()) {
        return "";
    }

    switch (source.value()) {
        case NesGameAdapterControllerSource::InferredPolicy:
            return "InferredPolicy";
        case NesGameAdapterControllerSource::ScriptedSetup:
            return "ScriptedSetup";
    }

    return "Unknown";
}

std::string formatRunnerState(TrainingRunner::State state)
{
    switch (state) {
        case TrainingRunner::State::Running:
            return "Running";
        case TrainingRunner::State::TimeExpired:
            return "TimeExpired";
        case TrainingRunner::State::OrganismDied:
            return "OrganismDied";
    }

    return "Unknown";
}

std::string formatSmbSnapshot(const std::optional<NesSuperMarioBrosFitnessSnapshot>& smbSnapshot)
{
    if (!smbSnapshot.has_value()) {
        return "none";
    }

    const NesSuperMarioBrosFitnessSnapshot& snapshot = smbSnapshot.value();
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "bestStage=" << static_cast<unsigned int>(snapshot.bestWorld) << "-"
        << static_cast<unsigned int>(snapshot.bestLevel)
        << ", bestAbsoluteX=" << snapshot.bestAbsoluteX
        << ", currentStage=" << static_cast<unsigned int>(snapshot.currentWorld) << "-"
        << static_cast<unsigned int>(snapshot.currentLevel)
        << ", currentAbsoluteX=" << snapshot.currentAbsoluteX
        << ", gameplayFrames=" << snapshot.gameplayFrames
        << ", framesSinceProgress=" << snapshot.framesSinceProgress
        << ", totalReward=" << snapshot.totalReward << ", done=" << formatBool(snapshot.done);
    return out.str();
}

std::string formatGenomeSelection(const GenomeSelection& selection)
{
    std::ostringstream out;
    out << "source=" << selection.sourceDescription;
    out << ", brain=" << formatBrainSpec(selection.brain);
    if (selection.genomeId.has_value()) {
        out << ", genome_id=" << selection.genomeId->toString();
    }
    if (selection.repositoryPath.has_value()) {
        out << ", repository_path=" << selection.repositoryPath->string();
    }
    return out.str();
}

std::string buildTraceCsvHeader()
{
    return "step_ordinal,sim_time,state,frame_advanced,rendered_frames_before,"
           "rendered_frames_after,advanced_frames,inferred_controller_mask,"
           "resolved_controller_mask,controller_source,controller_source_frame_index,"
           "reward_delta,evaluation_done,last_game_state_before,last_game_state_after,"
           "command_signature,command_outcome,debug_advanced_frame_count,debug_phase,"
           "debug_life_state,debug_lives,debug_world,debug_level,debug_absolute_x,"
           "debug_player_x_screen,debug_player_y_screen,debug_powerup_state,"
           "debug_setup_failure,debug_setup_script_active";
}

std::string buildTraceCsvRow(const TrainingRunner::FrameTrace& trace)
{
    const TrainingRunner::NesFrameTrace* nesTrace =
        trace.nes.has_value() ? &trace.nes.value() : nullptr;
    const NesGameAdapterDebugState* debugState =
        (nesTrace != nullptr && nesTrace->debugState.has_value()) ? &nesTrace->debugState.value()
                                                                  : nullptr;

    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << trace.stepOrdinal << ',' << trace.simTime << ',' << formatRunnerState(trace.state);
    out << ',' << (nesTrace != nullptr ? formatBool(nesTrace->frameAdvanced) : "");
    out << ','
        << (nesTrace != nullptr
                ? std::to_string(static_cast<unsigned long long>(nesTrace->renderedFramesBefore))
                : "");
    out << ','
        << (nesTrace != nullptr
                ? std::to_string(static_cast<unsigned long long>(nesTrace->renderedFramesAfter))
                : "");
    out << ','
        << (nesTrace != nullptr
                ? std::to_string(static_cast<unsigned long long>(nesTrace->advancedFrames))
                : "");
    out << ','
        << (nesTrace != nullptr
                ? std::to_string(static_cast<unsigned int>(nesTrace->inferredControllerMask))
                : "");
    out << ','
        << (nesTrace != nullptr
                ? std::to_string(static_cast<unsigned int>(nesTrace->resolvedControllerMask))
                : "");
    out << ',' << (nesTrace != nullptr ? formatControllerSource(nesTrace->controllerSource) : "");
    out << ','
        << (nesTrace != nullptr ? formatOptionalUint64(nesTrace->controllerSourceFrameIndex) : "");
    out << ',' << (nesTrace != nullptr ? std::to_string(nesTrace->rewardDelta) : "");
    out << ',' << (nesTrace != nullptr ? formatBool(nesTrace->evaluationDone) : "");
    out << ',' << (nesTrace != nullptr ? formatOptionalUint8(nesTrace->lastGameStateBefore) : "");
    out << ',' << (nesTrace != nullptr ? formatOptionalUint8(nesTrace->lastGameStateAfter) : "");
    out << ',' << (nesTrace != nullptr ? nesTrace->commandSignature : "");
    out << ',' << (nesTrace != nullptr ? nesTrace->commandOutcome : "");
    out << ','
        << (debugState != nullptr ? formatOptionalUint64(debugState->advancedFrameCount) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->phase) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->lifeState) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->lives) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->world) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->level) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint16(debugState->absoluteX) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->playerXScreen) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->playerYScreen) : "");
    out << ',' << (debugState != nullptr ? formatOptionalUint8(debugState->powerupState) : "");
    out << ',' << (debugState != nullptr ? formatOptionalBool(debugState->setupFailure) : "");
    out << ',' << (debugState != nullptr ? formatOptionalBool(debugState->setupScriptActive) : "");
    return out.str();
}

std::optional<TraceDivergence> findFirstTraceDivergence(
    const std::vector<TrainingRunner::FrameTrace>& baseline,
    const std::vector<TrainingRunner::FrameTrace>& candidate)
{
    const size_t sharedSize = std::min(baseline.size(), candidate.size());
    for (size_t i = 0; i < sharedSize; ++i) {
        const std::string baselineRow = buildTraceCsvRow(baseline[i]);
        const std::string candidateRow = buildTraceCsvRow(candidate[i]);
        if (baselineRow == candidateRow) {
            continue;
        }

        return TraceDivergence{
            .traceIndex = i,
            .stepOrdinal = baseline[i].stepOrdinal,
            .baselineRow = baselineRow,
            .candidateRow = candidateRow,
            .reason = "row_mismatch",
        };
    }

    if (baseline.size() != candidate.size()) {
        return TraceDivergence{
            .traceIndex = sharedSize,
            .stepOrdinal = sharedSize < baseline.size() ? baseline[sharedSize].stepOrdinal : 0u,
            .baselineRow =
                sharedSize < baseline.size() ? buildTraceCsvRow(baseline[sharedSize]) : "",
            .candidateRow =
                sharedSize < candidate.size() ? buildTraceCsvRow(candidate[sharedSize]) : "",
            .reason = "trace_length_mismatch",
        };
    }

    return std::nullopt;
}

bool writeTraceCsv(const std::filesystem::path& path, const SmbTraceHarnessRun& run)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    stream << buildTraceCsvHeader() << "\n";
    for (const TrainingRunner::FrameTrace& trace : run.traces) {
        stream << buildTraceCsvRow(trace) << "\n";
    }

    return stream.good();
}

bool writeSummary(
    const std::filesystem::path& path,
    const GenomeSelection& selection,
    double maxSimulationTime,
    const std::vector<SmbTraceHarnessRun>& runs,
    const std::vector<std::optional<TraceDivergence>>& divergences)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    stream << "SMB TrainingRunner trace harness\n";
    stream << "genome_seed=" << kSmbHarnessGenomeSeed << "\n";
    stream << "max_simulation_time=" << maxSimulationTime << "\n";
    stream << "genome_selection=" << formatGenomeSelection(selection) << "\n";
    stream << "run_count=" << runs.size() << "\n\n";

    for (size_t i = 0; i < runs.size(); ++i) {
        const SmbTraceHarnessRun& run = runs[i];
        stream << "run[" << i << "]"
               << " state=" << formatRunnerState(run.status.state) << " sim_time=" << std::fixed
               << std::setprecision(6) << run.status.simTime
               << " nes_frames=" << run.status.nesFramesSurvived
               << " nes_reward_total=" << run.status.nesRewardTotal
               << " trace_frames=" << run.traces.size() << "\n";
        stream << "snapshot[" << i << "] " << formatSmbSnapshot(run.smbSnapshot) << "\n";
    }

    stream << "\n";
    for (size_t i = 0; i < divergences.size(); ++i) {
        stream << "baseline_vs_run[" << (i + 1) << "] ";
        if (!divergences[i].has_value()) {
            stream << "no_divergence\n";
            continue;
        }

        const TraceDivergence& divergence = divergences[i].value();
        stream << divergence.reason << " trace_index=" << divergence.traceIndex
               << " step_ordinal=" << divergence.stepOrdinal << "\n";
        stream << "baseline_row=" << divergence.baselineRow << "\n";
        stream << "candidate_row=" << divergence.candidateRow << "\n";
    }

    return stream.good();
}

SmbTraceHarnessRun runSmbTraceHarnessOnce(
    const std::filesystem::path& romPath,
    const GenomeSelection& selection,
    EvolutionConfig evolutionConfig,
    GenomeRepository& genomeRepository)
{
    SmbTraceHarnessRun run;
    const TrainingSpec spec = makeSmbHarnessTrainingSpec();
    TrainingRunner::Individual individual;
    individual.brain = selection.brain;
    individual.scenarioId = Scenario::EnumType::NesSuperMarioBros;
    individual.genome = selection.genome;
    const Config::NesSuperMarioBros smbConfig = makeSmbHarnessScenarioConfig(romPath);

    TrainingRunner::Config runnerConfig{
        .brainRegistry = TrainingBrainRegistry::createDefault(),
        .nesGameAdapterRegistry = NesGameAdapterRegistry::createDefault(),
        .duckClockSpawnLeftFirst = std::nullopt,
        .duckClockSpawnRngSeed = std::nullopt,
        .frameTraceSink =
            [&run](const TrainingRunner::FrameTrace& trace) { run.traces.push_back(trace); },
        .scenarioConfigOverride = ScenarioConfig{ smbConfig },
    };
    TrainingRunner runner(spec, individual, evolutionConfig, genomeRepository, runnerConfig);

    TrainingRunner::Status status = runner.getStatus();
    const int maxSteps =
        static_cast<int>(std::ceil(evolutionConfig.maxSimulationTime / TrainingRunner::TIMESTEP))
        + 16;
    int steps = 0;
    while (status.state == TrainingRunner::State::Running && steps < maxSteps) {
        status = runner.step(1);
        steps++;
    }

    run.status = status;
    if (const auto* snapshot =
            std::get_if<NesSuperMarioBrosFitnessSnapshot>(&runner.getNesFitnessDetails());
        snapshot != nullptr) {
        run.smbSnapshot = *snapshot;
    }
    return run;
}

class ScopedEnvVarOverride {
public:
    ScopedEnvVarOverride(const char* envVarName, std::optional<std::string> value)
        : envVarName_(envVarName), previousValue_(getEnvValue(envVarName))
    {
        apply(value);
    }

    ~ScopedEnvVarOverride() { apply(previousValue_); }

private:
    void apply(const std::optional<std::string>& value)
    {
        if (value.has_value()) {
            setenv(envVarName_.c_str(), value->c_str(), 1);
            return;
        }

        unsetenv(envVarName_.c_str());
    }

    std::string envVarName_;
    std::optional<std::string> previousValue_ = std::nullopt;
};

} // namespace

TEST(TrainingRunnerTraceHarnessTest, ResolveConfiguredGenomeSelectionLoadsRepositoryGenomeOverride)
{
    const std::filesystem::path dbPath = std::filesystem::temp_directory_path()
        / ("smb_trace_harness_" + UUID::generate().toString() + ".db");

    const Genome expectedGenome = makeSmbHarnessGenome();
    const GenomeId genomeId = UUID::generate();
    {
        GenomeRepository repository(dbPath);
        repository.store(
            genomeId,
            expectedGenome,
            GenomeMetadata{
                .name = "SMB harness override genome",
                .fitness = 1234.5,
                .robustFitness = 1234.5,
                .robustEvalCount = 1,
                .robustFitnessSamples = { 1234.5 },
                .generation = 7,
                .createdTimestamp = 1234567890,
                .scenarioId = Scenario::EnumType::NesSuperMarioBros,
                .notes = "",
                .organismType = OrganismType::NES_DUCK,
                .brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2,
                .brainVariant = std::nullopt,
                .trainingSessionId = std::nullopt,
            });
    }

    {
        ScopedEnvVarOverride genomeDbPathOverride(kSmbHarnessGenomeDbPathEnv, dbPath.string());
        ScopedEnvVarOverride genomeIdOverride(kSmbHarnessGenomeIdEnv, genomeId.toString());

        const GenomeSelectionResult result = resolveConfiguredSmbHarnessGenomeSelection();
        ASSERT_TRUE(result.selection.has_value()) << result.error;
        EXPECT_EQ(result.selection->brain.brainKind, TrainingBrainKind::DuckNeuralNetRecurrentV2);
        EXPECT_EQ(result.selection->genome, expectedGenome);
        EXPECT_EQ(result.selection->genomeId, std::optional<GenomeId>(genomeId));
        EXPECT_EQ(result.selection->repositoryPath, std::optional<std::filesystem::path>(dbPath));
        EXPECT_EQ(result.selection->sourceDescription, "repository-genome");
    }

    std::filesystem::remove(dbPath);
}

// Manual harness for SMB trace capture and replay diffs.
//
// This stays disabled because it writes CSV artifacts, prints run details, and is intended for
// targeted investigations. Run it explicitly with
// `./build-debug/bin/dirtsim-tests
// --gtest_filter=TrainingRunnerTraceHarnessTest.DISABLED_ManualTraceHarness_WritesRepeatedSmbRunData
// --gtest_also_run_disabled_tests` and optionally set `DIRTSIM_TRAINING_TRACE_GENOME_DB_PATH`,
// `DIRTSIM_TRAINING_TRACE_GENOME_ID`, and `DIRTSIM_TRAINING_TRACE_MAX_SIMULATION_TIME`.
TEST(TrainingRunnerTraceHarnessTest, DISABLED_ManualTraceHarness_WritesRepeatedSmbRunData)
{
    const std::optional<std::filesystem::path> romPath = DirtSim::Test::resolveSmbRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "SMB ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or add "
                        "apps/testdata/roms/smb.nes.";
    }

    const MaxSimulationTimeResult maxSimulationTimeResult =
        resolveConfiguredSmbHarnessMaxSimulationTime();
    ASSERT_TRUE(maxSimulationTimeResult.error.empty()) << maxSimulationTimeResult.error;
    const double maxSimulationTime = maxSimulationTimeResult.value;

    EvolutionConfig evolutionConfig;
    evolutionConfig.maxSimulationTime = maxSimulationTime;

    const GenomeSelectionResult selectionResult = resolveConfiguredSmbHarnessGenomeSelection();
    ASSERT_TRUE(selectionResult.selection.has_value()) << selectionResult.error;
    const GenomeSelection& selection = selectionResult.selection.value();

    GenomeRepository genomeRepository;
    std::cout << "SMB harness genome selection: " << formatGenomeSelection(selection) << "\n";
    std::cout << "SMB harness max simulation time: " << maxSimulationTime << "\n";

    std::vector<SmbTraceHarnessRun> runs;
    runs.reserve(kSmbHarnessRunCount);
    for (int runIndex = 0; runIndex < kSmbHarnessRunCount; ++runIndex) {
        runs.push_back(
            runSmbTraceHarnessOnce(romPath.value(), selection, evolutionConfig, genomeRepository));
    }

    const std::filesystem::path outputDir =
        std::filesystem::path(::testing::TempDir()) / "smb_training_trace_harness";
    std::filesystem::create_directories(outputDir);

    std::vector<std::optional<TraceDivergence>> divergences;
    if (!runs.empty()) {
        divergences.reserve(runs.size() - 1u);
    }

    for (size_t i = 0; i < runs.size(); ++i) {
        ASSERT_FALSE(runs[i].traces.empty()) << "Run " << i << " produced no trace frames";

        const std::filesystem::path tracePath =
            outputDir / ("smb_training_trace_run_" + std::to_string(i) + ".csv");
        EXPECT_TRUE(writeTraceCsv(tracePath, runs[i]));
        EXPECT_TRUE(std::filesystem::exists(tracePath));
        EXPECT_GT(std::filesystem::file_size(tracePath), 0u);
        std::cout << "Wrote SMB training trace run " << i << ": " << tracePath.string() << "\n";

        if (i == 0u) {
            continue;
        }

        divergences.push_back(findFirstTraceDivergence(runs.front().traces, runs[i].traces));
        if (divergences.back().has_value()) {
            const TraceDivergence& divergence = divergences.back().value();
            std::cout << "SMB harness divergence vs baseline run " << i << " at trace index "
                      << divergence.traceIndex << ", step " << divergence.stepOrdinal << ": "
                      << divergence.reason << "\n";
        }
        else {
            std::cout << "SMB harness run " << i << " matched the baseline trace.\n";
        }
    }

    const std::filesystem::path summaryPath = outputDir / "smb_training_trace_summary.txt";
    EXPECT_TRUE(writeSummary(summaryPath, selection, maxSimulationTime, runs, divergences));
    EXPECT_TRUE(std::filesystem::exists(summaryPath));
    EXPECT_GT(std::filesystem::file_size(summaryPath), 0u);
    std::cout << "Wrote SMB training trace harness summary: " << summaryPath.string() << "\n";
}

} // namespace DirtSim
