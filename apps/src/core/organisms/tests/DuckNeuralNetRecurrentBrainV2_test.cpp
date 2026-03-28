#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"

#include "DuckTestUtils.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/organisms/Duck.h"
#include "core/organisms/OrganismManager.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <vector>

using namespace DirtSim;
using namespace DirtSim::Test;

namespace {

constexpr uint32_t kBenchmarkGenomeSeed = 42u;
constexpr int kBenchmarkDefaultIterations = 10000;
constexpr int kBenchmarkDefaultRepeats = 5;
constexpr int kBenchmarkDefaultWarmupIterations = 1000;
constexpr double kBenchmarkDeltaTime = 0.016;
constexpr const char* kBenchmarkIterationsEnv = "DIRTSIM_DUCK_RNN_BENCH_ITERATIONS";
constexpr const char* kBenchmarkRepeatsEnv = "DIRTSIM_DUCK_RNN_BENCH_REPEATS";
constexpr const char* kBenchmarkWarmupIterationsEnv = "DIRTSIM_DUCK_RNN_BENCH_WARMUP_ITERATIONS";
constexpr int kInputHistogramSize =
    DuckSensoryData::GRID_SIZE * DuckSensoryData::GRID_SIZE * DuckSensoryData::NUM_MATERIALS;
constexpr int kInputSize = kInputHistogramSize + 4 + DuckSensoryData::SPECIAL_SENSE_COUNT + 2;
constexpr int kH1Size = 64;
constexpr int kH2Size = 32;
constexpr int kOutputSize = 4;
constexpr int kWXH1Size = kInputSize * kH1Size;
constexpr int kWH1H1Size = kH1Size * kH1Size;
constexpr int kBH1Size = kH1Size;
constexpr int kAlpha1LogitSize = kH1Size;
constexpr int kWH1H2Size = kH1Size * kH2Size;
constexpr int kWH2H2Size = kH2Size * kH2Size;
constexpr int kBH2Size = kH2Size;
constexpr int kAlpha2LogitSize = kH2Size;
constexpr int kWH2OSize = kH2Size * kOutputSize;
constexpr int kBOSize = kOutputSize;
constexpr float kMaxAlphaLogit = 4.0f;

size_t energyInputIndex()
{
    return static_cast<size_t>(kInputHistogramSize + 4 + DuckSensoryData::SPECIAL_SENSE_COUNT);
}

size_t wXh1Index(size_t inputIndex, size_t hiddenIndex)
{
    return inputIndex * static_cast<size_t>(kH1Size) + hiddenIndex;
}

size_t alpha1LogitIndex(size_t hiddenIndex)
{
    return static_cast<size_t>(kWXH1Size + kWH1H1Size + kBH1Size) + hiddenIndex;
}

size_t wH1H2Index(size_t h1Index, size_t h2Index)
{
    return static_cast<size_t>(kWXH1Size + kWH1H1Size + kBH1Size + kAlpha1LogitSize)
        + (h1Index * static_cast<size_t>(kH2Size)) + h2Index;
}

size_t alpha2LogitIndex(size_t hiddenIndex)
{
    return static_cast<size_t>(
               kWXH1Size + kWH1H1Size + kBH1Size + kAlpha1LogitSize + kWH1H2Size + kWH2H2Size
               + kBH2Size)
        + hiddenIndex;
}

size_t wH2OIndex(size_t h2Index, size_t outputIndex)
{
    return static_cast<size_t>(
               kWXH1Size + kWH1H1Size + kBH1Size + kAlpha1LogitSize + kWH1H2Size + kWH2H2Size
               + kBH2Size + kAlpha2LogitSize)
        + (h2Index * static_cast<size_t>(kOutputSize)) + outputIndex;
}

Genome makeNegativePropagationGenome()
{
    Genome genome(
        static_cast<size_t>(
            kWXH1Size + kWH1H1Size + kBH1Size + kAlpha1LogitSize + kWH1H2Size + kWH2H2Size
            + kBH2Size + kAlpha2LogitSize + kWH2OSize + kBOSize),
        0.0f);

    genome.weights[wXh1Index(energyInputIndex(), 0)] = -2.0f;
    genome.weights[alpha1LogitIndex(0)] = kMaxAlphaLogit;
    genome.weights[wH1H2Index(0, 0)] = 2.0f;
    genome.weights[alpha2LogitIndex(0)] = kMaxAlphaLogit;
    genome.weights[wH2OIndex(0, 0)] = 10.0f;
    return genome;
}

struct DuckBrainBenchmarkSetup {
    std::unique_ptr<World> world;
    Duck* duck = nullptr;
    DuckNeuralNetRecurrentBrainV2* brain = nullptr;
    DuckSensoryData fixedSensory{};
};

struct ManualBenchmarkStats {
    std::string name;
    std::vector<double> nsPerIterationSamples;
    double sink = 0.0;

    double meanNsPerIteration() const
    {
        if (nsPerIterationSamples.empty()) {
            return 0.0;
        }

        double sum = 0.0;
        for (const double sample : nsPerIterationSamples) {
            sum += sample;
        }
        return sum / static_cast<double>(nsPerIterationSamples.size());
    }

    double medianNsPerIteration() const
    {
        if (nsPerIterationSamples.empty()) {
            return 0.0;
        }

        std::vector<double> sorted = nsPerIterationSamples;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    double minNsPerIteration() const
    {
        if (nsPerIterationSamples.empty()) {
            return 0.0;
        }

        return *std::min_element(nsPerIterationSamples.begin(), nsPerIterationSamples.end());
    }
};

struct LoggerLevelGuard {
    std::shared_ptr<spdlog::logger> logger;
    spdlog::level::level_enum previousLevel = spdlog::level::info;

    ~LoggerLevelGuard()
    {
        if (logger) {
            logger->set_level(previousLevel);
        }
    }
};

std::optional<std::string> getEnvValue(const char* envVarName)
{
    if (const char* value = std::getenv(envVarName); value != nullptr && value[0] != '\0') {
        return std::string(value);
    }

    return std::nullopt;
}

bool tryParsePositiveInt(const std::string& text, int& outValue)
{
    errno = 0;
    char* parseEnd = nullptr;
    const long parsed = std::strtol(text.c_str(), &parseEnd, 10);
    if (errno != 0 || parseEnd == text.c_str() || *parseEnd != '\0' || parsed <= 0) {
        return false;
    }

    outValue = static_cast<int>(parsed);
    return true;
}

int resolvePositiveIntEnvOrDefault(const char* envVarName, int fallback)
{
    const std::optional<std::string> configured = getEnvValue(envVarName);
    if (!configured.has_value()) {
        return fallback;
    }

    int parsed = 0;
    if (!tryParsePositiveInt(configured.value(), parsed)) {
        ADD_FAILURE() << "Invalid " << envVarName << "='" << configured.value()
                      << "'. Expected a positive integer.";
        return fallback;
    }

    return parsed;
}

void stampBenchmarkPattern(World& world, Vector2i center)
{
    static constexpr std::array<Material::EnumType, 8> kPatternMaterials = {
        Material::EnumType::Dirt, Material::EnumType::Leaf,  Material::EnumType::Metal,
        Material::EnumType::Root, Material::EnumType::Sand,  Material::EnumType::Seed,
        Material::EnumType::Wall, Material::EnumType::Water,
    };

    WorldData& data = world.getData();
    constexpr int radius = DuckSensoryData::GRID_SIZE / 2;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = center.x + dx;
            const int y = center.y + dy;
            if (!data.inBounds(x, y) || (x == center.x && y == center.y)) {
                continue;
            }

            if ((std::abs(dx) + std::abs(dy)) % 5 == 0) {
                data.at(x, y).replaceMaterial(Material::EnumType::Air, 0.0);
                continue;
            }

            const size_t patternIndex =
                static_cast<size_t>(std::abs((dx * 17) + (dy * 31) + x + y));
            data.at(x, y).replaceMaterial(
                kPatternMaterials[patternIndex % kPatternMaterials.size()], 1.0);
        }
    }
}

DuckSensoryData makeBenchmarkSensory(const Duck& duck, const World& world)
{
    DuckSensoryData sensory = duck.gatherSensoryData(world, kBenchmarkDeltaTime);
    sensory.energy = 0.73f;
    sensory.facing_x = -1.0f;
    sensory.health = 0.91f;
    sensory.on_ground = true;
    sensory.velocity = Vector2d{ 1.5, -0.75 };
    for (int i = 0; i < DuckSensoryData::SPECIAL_SENSE_COUNT; ++i) {
        sensory.special_senses[static_cast<size_t>(i)] = static_cast<double>((i % 7) - 3) / 3.0;
    }
    return sensory;
}

double consumeSensoryData(const DuckSensoryData& sensory)
{
    constexpr int center = DuckSensoryData::GRID_SIZE / 2;
    return sensory.energy + sensory.health + sensory.facing_x + sensory.velocity.x
        + sensory.velocity.y + sensory.special_senses[0] + sensory.special_senses[7]
        + sensory.special_senses[DuckSensoryData::SPECIAL_SENSE_COUNT - 1]
        + sensory.material_histograms[0][0][0] + sensory.material_histograms[center][center][1]
        + sensory.material_histograms[DuckSensoryData::GRID_SIZE - 1]
                                     [DuckSensoryData::GRID_SIZE - 1][2];
}

DuckBrainBenchmarkSetup createDuckBrainBenchmarkSetup()
{
    constexpr int worldWidth = 48;
    constexpr int worldHeight = 32;
    const Vector2i duckPosition{ worldWidth / 2, worldHeight / 2 };

    DuckBrainBenchmarkSetup setup;
    setup.world = createFlatWorld(worldWidth, worldHeight);

    std::mt19937 rng(kBenchmarkGenomeSeed);
    const Genome genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng);
    auto brain = std::make_unique<DuckNeuralNetRecurrentBrainV2>(genome);
    setup.brain = brain.get();

    OrganismManager& manager = setup.world->getOrganismManager();
    const OrganismId duckId = manager.createDuck(
        *setup.world,
        static_cast<uint32_t>(duckPosition.x),
        static_cast<uint32_t>(duckPosition.y),
        std::move(brain));
    setup.duck = manager.getDuck(duckId);
    DIRTSIM_ASSERT(setup.duck != nullptr, "Duck brain benchmark setup requires a duck");

    stampBenchmarkPattern(*setup.world, duckPosition);
    setup.fixedSensory = makeBenchmarkSensory(*setup.duck, *setup.world);
    return setup;
}

template <typename Fn>
ManualBenchmarkStats runManualBenchmark(
    const std::string& name, int warmupIterations, int iterations, int repeats, Fn&& fn)
{
    volatile double sink = 0.0;
    for (int i = 0; i < warmupIterations; ++i) {
        sink += fn();
    }

    ManualBenchmarkStats stats{};
    stats.name = name;
    stats.nsPerIterationSamples.reserve(static_cast<size_t>(repeats));
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) {
            sink += fn();
        }
        const auto end = std::chrono::steady_clock::now();
        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        stats.nsPerIterationSamples.push_back(elapsedNs / static_cast<double>(iterations));
    }

    stats.sink = sink;
    return stats;
}

void printManualBenchmarkSummary(
    const std::vector<ManualBenchmarkStats>& stats,
    int warmupIterations,
    int iterations,
    int repeats)
{
    std::cerr << "DuckNeuralNetRecurrentBrainV2 manual benchmark.\n";
    std::cerr << "  warmup_iterations=" << warmupIterations << "\n";
    std::cerr << "  iterations=" << iterations << "\n";
    std::cerr << "  repeats=" << repeats << "\n\n";

    std::cerr << std::left << std::setw(20) << "Benchmark" << std::right << std::setw(14)
              << "Min ns/op" << std::setw(14) << "Median ns/op" << std::setw(14) << "Mean ns/op"
              << "\n";
    std::cerr << std::string(62, '-') << "\n";

    for (const auto& entry : stats) {
        std::cerr << std::left << std::setw(20) << entry.name << std::right << std::fixed
                  << std::setprecision(1) << std::setw(14) << entry.minNsPerIteration()
                  << std::setw(14) << entry.medianNsPerIteration() << std::setw(14)
                  << entry.meanNsPerIteration() << "\n";
    }
    std::cerr << std::endl;
}

} // namespace

TEST(DuckNeuralNetRecurrentBrainV2Test, GenomeRoundTripPreservesWeights)
{
    std::mt19937 rng(42);
    Genome genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng);

    ASSERT_TRUE(DuckNeuralNetRecurrentBrainV2::isGenomeCompatible(genome));

    DuckNeuralNetRecurrentBrainV2 brain(genome);
    Genome roundTrip = brain.getGenome();

    EXPECT_EQ(roundTrip.weights.size(), genome.weights.size());
    EXPECT_EQ(roundTrip.weights, genome.weights);
}

TEST(DuckNeuralNetRecurrentBrainV2Test, GenomeCompatibilityRejectsWrongSize)
{
    Genome genome;
    genome.weights.resize(10);

    EXPECT_FALSE(DuckNeuralNetRecurrentBrainV2::isGenomeCompatible(genome));
}

TEST(DuckNeuralNetRecurrentBrainV2Test, GenomeLayoutMatchesRandomGenomeSize)
{
    std::mt19937 rng(42);
    const Genome genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng);
    const GenomeLayout layout = DuckNeuralNetRecurrentBrainV2::getGenomeLayout();

    EXPECT_EQ(layout.totalSize(), static_cast<int>(genome.weights.size()));
}

TEST(DuckNeuralNetRecurrentBrainV2Test, GenomeLayoutUsesCoarseMutationDomains)
{
    const GenomeLayout layout = DuckNeuralNetRecurrentBrainV2::getGenomeLayout();

    ASSERT_EQ(layout.segments.size(), 5u);
    EXPECT_EQ(layout.segments[0].name, "input_h1");
    EXPECT_EQ(layout.segments[0].size, 284672);
    EXPECT_EQ(layout.segments[1].name, "h1_recurrent");
    EXPECT_EQ(layout.segments[1].size, 4224);
    EXPECT_EQ(layout.segments[2].name, "h1_to_h2");
    EXPECT_EQ(layout.segments[2].size, 2048);
    EXPECT_EQ(layout.segments[3].name, "h2_recurrent");
    EXPECT_EQ(layout.segments[3].size, 1088);
    EXPECT_EQ(layout.segments[4].name, "output");
    EXPECT_EQ(layout.segments[4].size, 132);
}

TEST(DuckNeuralNetRecurrentBrainV2Test, NegativeSignalPropagatesThroughBothHiddenLayers)
{
    const Genome genome = makeNegativePropagationGenome();
    ASSERT_TRUE(DuckNeuralNetRecurrentBrainV2::isGenomeCompatible(genome));

    DuckNeuralNetRecurrentBrainV2 brain(genome);
    DuckSensoryData sensory{};
    sensory.energy = 1.0f;
    sensory.health = 0.0f;
    sensory.facing_x = 0.0f;
    sensory.on_ground = false;
    sensory.velocity = Vector2d{ 0.0, 0.0 };

    const auto output = brain.inferControllerOutput(sensory);

    EXPECT_LT(output.xRaw, -0.2f);
    EXPECT_LT(output.x, -0.2f);
    EXPECT_FALSE(output.a);
    EXPECT_FALSE(output.b);
}

TEST(DuckNeuralNetRecurrentBrainV2Test, RandomGenomesProduceCommandDiversity)
{
    // Suppress noisy per-frame duck logging.
    LoggingChannels::get(LogChannel::Brain)->set_level(spdlog::level::warn);

    constexpr int NUM_BRAINS = 10;
    constexpr double SIM_SECONDS = 10.0;
    constexpr double DT = 0.016;
    constexpr int STEPS = static_cast<int>(SIM_SECONDS / DT);

    const auto seed = std::random_device{}();
    std::cerr << "Seed: " << seed << "\n\n";
    std::mt19937 rng(seed);

    // Track command counts per brain for cross-brain analysis.
    std::set<std::string> all_signatures;
    std::vector<std::map<std::string, int>> brain_commands(NUM_BRAINS);

    for (int i = 0; i < NUM_BRAINS; ++i) {
        const Genome genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng);
        auto brain = std::make_unique<DuckNeuralNetRecurrentBrainV2>(genome);

        auto world = createFlatWorld(32, 20);
        OrganismManager& manager = world->getOrganismManager();
        // Spawn on the floor (height-2).
        const OrganismId id = manager.createDuck(*world, 16, 18, std::move(brain));
        Duck* duck = manager.getDuck(id);
        ASSERT_NE(duck, nullptr);

        for (int step = 0; step < STEPS; ++step) {
            world->advanceTime(DT);
        }

        const auto commands = duck->getTopCommandSignatures(20);
        int total = 0;
        for (const auto& [sig, count] : commands) {
            total += count;
            brain_commands[i][sig] = count;
            all_signatures.insert(sig);
        }

        std::cerr << "Brain " << i << ":\n";
        for (const auto& [sig, count] : commands) {
            const double pct = 100.0 * count / total;
            std::cerr << "  " << sig << ": " << count << " (" << std::fixed << std::setprecision(1)
                      << pct << "%)\n";
        }
        std::cerr << "  Total: " << total << "\n";
        std::cerr << "  Unique commands: " << commands.size() << "\n\n";
    }

    // Cross-brain spread analysis: show how each command varies across brains.
    std::cerr << "=== Command spread across " << NUM_BRAINS << " brains ===\n";
    std::cerr << std::left << std::setw(30) << "Command" << std::right << std::setw(8) << "Min%"
              << std::setw(8) << "Max%" << std::setw(8) << "Avg%" << std::setw(8) << "Brains"
              << "\n";
    std::cerr << std::string(62, '-') << "\n";

    for (const auto& sig : all_signatures) {
        double min_pct = 100.0;
        double max_pct = 0.0;
        double sum_pct = 0.0;
        int brains_with_cmd = 0;

        for (int i = 0; i < NUM_BRAINS; ++i) {
            // Compute this brain's total.
            int total = 0;
            for (const auto& [s, c] : brain_commands[i]) {
                total += c;
            }
            if (total == 0) continue;

            auto it = brain_commands[i].find(sig);
            const double pct = (it != brain_commands[i].end()) ? 100.0 * it->second / total : 0.0;

            min_pct = std::min(min_pct, pct);
            max_pct = std::max(max_pct, pct);
            sum_pct += pct;
            if (pct > 0.0) ++brains_with_cmd;
        }

        const double avg_pct = sum_pct / NUM_BRAINS;
        std::cerr << std::left << std::setw(30) << sig << std::right << std::fixed
                  << std::setprecision(1) << std::setw(8) << min_pct << std::setw(8) << max_pct
                  << std::setw(8) << avg_pct << std::setw(8) << brains_with_cmd << "/" << NUM_BRAINS
                  << "\n";
    }
    std::cerr << "\n";

    // Restore logging.
    LoggingChannels::get(LogChannel::Brain)->set_level(spdlog::level::info);
}

// Manual benchmark for direct duck recurrent brain profiling.
//
// This stays disabled because it prints timing summaries to stderr and is intended for targeted
// investigations. Run it explicitly with
// `./build-debug/bin/dirtsim-tests
// --gtest_filter=DuckNeuralNetRecurrentBrainV2Test.DISABLED_ManualBenchmark_BrainAndSensoryPaths
// --gtest_also_run_disabled_tests` and optionally set `DIRTSIM_DUCK_RNN_BENCH_ITERATIONS`,
// `DIRTSIM_DUCK_RNN_BENCH_REPEATS`, and `DIRTSIM_DUCK_RNN_BENCH_WARMUP_ITERATIONS`.
TEST(DuckNeuralNetRecurrentBrainV2Test, DISABLED_ManualBenchmark_BrainAndSensoryPaths)
{
    LoggerLevelGuard brainLogGuard{
        .logger = LoggingChannels::get(LogChannel::Brain),
        .previousLevel = LoggingChannels::get(LogChannel::Brain)->level(),
    };
    brainLogGuard.logger->set_level(spdlog::level::warn);

    const int iterations =
        resolvePositiveIntEnvOrDefault(kBenchmarkIterationsEnv, kBenchmarkDefaultIterations);
    const int repeats =
        resolvePositiveIntEnvOrDefault(kBenchmarkRepeatsEnv, kBenchmarkDefaultRepeats);
    const int warmupIterations = resolvePositiveIntEnvOrDefault(
        kBenchmarkWarmupIterationsEnv, kBenchmarkDefaultWarmupIterations);
    ASSERT_FALSE(HasFailure());

    std::vector<ManualBenchmarkStats> stats;

    {
        DuckBrainBenchmarkSetup setup = createDuckBrainBenchmarkSetup();
        stats.push_back(
            runManualBenchmark("brain.think", warmupIterations, iterations, repeats, [&setup]() {
                setup.brain->think(*setup.duck, setup.fixedSensory, kBenchmarkDeltaTime);
                return static_cast<double>(setup.duck->getEffortSampleCount());
            }));
    }

    {
        DuckBrainBenchmarkSetup setup = createDuckBrainBenchmarkSetup();
        stats.push_back(runManualBenchmark(
            "gatherSensoryData", warmupIterations, iterations, repeats, [&setup]() {
                const DuckSensoryData sensory =
                    setup.duck->gatherSensoryData(*setup.world, kBenchmarkDeltaTime);
                return consumeSensoryData(sensory);
            }));
    }

    {
        DuckBrainBenchmarkSetup setup = createDuckBrainBenchmarkSetup();
        stats.push_back(
            runManualBenchmark("gather+think", warmupIterations, iterations, repeats, [&setup]() {
                const DuckSensoryData sensory =
                    setup.duck->gatherSensoryData(*setup.world, kBenchmarkDeltaTime);
                setup.brain->think(*setup.duck, sensory, kBenchmarkDeltaTime);
                return consumeSensoryData(sensory)
                    + static_cast<double>(setup.duck->getEffortSampleCount());
            }));
    }

    printManualBenchmarkSummary(stats, warmupIterations, iterations, repeats);

    for (const auto& entry : stats) {
        EXPECT_GT(entry.meanNsPerIteration(), 0.0);
    }
}
