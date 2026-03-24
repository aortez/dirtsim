#include "core/Cell.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldRegionActivityTracker.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/DamBreakScenario.h"
#include "core/scenarios/EmptyScenario.h"
#include "core/scenarios/GooseTestScenario.h"
#include "core/scenarios/SandboxScenario.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/TreeGerminationScenario.h"
#include "core/scenarios/clock_scenario/RainEvent.h"
#include "core/water/MacProjectionWaterSim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace DirtSim;

namespace {

constexpr float kOccupiedVolumeThreshold = 0.05f;
constexpr float kNearlyFullVolumeThreshold = 0.95f;

float sumVolume(const WaterVolumeView& view)
{
    float total = 0.0f;
    for (float v : view.volume) {
        total += v;
    }
    return total;
}

float sumVolumeInRows(const WaterVolumeView& view, int yBeginInclusive, int yEndExclusive)
{
    float total = 0.0f;
    for (int y = yBeginInclusive; y < yEndExclusive; ++y) {
        for (int x = 0; x < view.width; ++x) {
            total += view.volume[static_cast<size_t>(y) * view.width + x];
        }
    }
    return total;
}

float sumVolumeInColumns(const WaterVolumeView& view, int xBeginInclusive, int xEndExclusive)
{
    float total = 0.0f;
    for (int y = 0; y < view.height; ++y) {
        for (int x = xBeginInclusive; x < xEndExclusive; ++x) {
            total += view.volume[static_cast<size_t>(y) * view.width + x];
        }
    }
    return total;
}

float computeVolumeCenterX(const WaterVolumeView& view)
{
    float weightedSum = 0.0f;
    float total = 0.0f;
    for (int y = 0; y < view.height; ++y) {
        for (int x = 0; x < view.width; ++x) {
            const float volume = view.volume[static_cast<size_t>(y) * view.width + x];
            weightedSum += static_cast<float>(x) * volume;
            total += volume;
        }
    }

    if (total <= 0.0f) {
        return 0.0f;
    }

    return weightedSum / total;
}

int countCellsAtOrAbove(
    const WaterVolumeView& view, int yBeginInclusive, int yEndExclusive, float minVolume)
{
    int count = 0;
    for (int y = yBeginInclusive; y < yEndExclusive; ++y) {
        for (int x = 0; x < view.width; ++x) {
            if (view.volume[static_cast<size_t>(y) * view.width + x] >= minVolume) {
                ++count;
            }
        }
    }
    return count;
}

bool hasLegacyWaterCells(const World& world)
{
    const WorldData& data = world.getData();
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            if (data.at(x, y).material_type == Material::EnumType::Water) {
                return true;
            }
        }
    }

    return false;
}

void expectNoResidualMacWater(const World& world)
{
    WaterVolumeView volumeView{};
    ASSERT_TRUE(world.tryGetWaterVolumeView(volumeView));
    EXPECT_NEAR(sumVolume(volumeView), 0.0f, 1e-6f);
    EXPECT_FALSE(hasLegacyWaterCells(world));
}

struct WaterShapeMetrics {
    int activeColumnCount = 0;
    float averageFillPerOccupiedCell = 0.0f;
    float bottomSixRowsFraction = 0.0f;
    float centerX = 0.0f;
    float columnVolumeStdDev = 0.0f;
    int occupiedCellCount = 0;
    int occupiedSpanWidth = 0;
    int partialCellCount = 0;
    float partialCellsPerActiveColumn = 0.0f;
    float partialVolumeFraction = 0.0f;
    float surfaceYStdDev = 0.0f;
    float totalVolume = 0.0f;
};

struct SandboxSizeCase {
    int height = 0;
    int width = 0;
};

struct WaterColumnDimensions {
    int height = 0;
    int width = 0;
};

class StartOfFrameWaterScenario final : public ScenarioRunner {
public:
    const ScenarioMetadata& getMetadata() const override { return metadata_; }

    ScenarioConfig getConfig() const override { return Config::Empty{}; }

    void setConfig(const ScenarioConfig& /*config*/, World& /*world*/) override {}

    void setup(World& world) override
    {
        WorldData& data = world.getData();
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                data.at(x, y) = Cell{};
            }
        }
        world.clearAllBulkWater();

        for (int x = 0; x < data.width; ++x) {
            world.replaceMaterialAtCell(
                Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(data.height - 1) },
                Material::EnumType::Wall);
        }
        for (int y = 0; y < data.height; ++y) {
            world.replaceMaterialAtCell(
                Vector2s{ 0, static_cast<int16_t>(y) }, Material::EnumType::Wall);
            world.replaceMaterialAtCell(
                Vector2s{ static_cast<int16_t>(data.width - 1), static_cast<int16_t>(y) },
                Material::EnumType::Wall);
        }

        didAuthorWater_ = false;
    }

    void reset(World& world) override { setup(world); }

    void tick(World& world, double /*deltaTime*/) override
    {
        if (didAuthorWater_) {
            return;
        }

        world.addBulkWaterAtCell(3, 6, 1.0f);
        didAuthorWater_ = true;
    }

private:
    ScenarioMetadata metadata_{
        .kind = ScenarioKind::GridWorld,
        .name = "StartOfFrameWaterScenario",
        .description = "Test scenario that authors water during tick.",
        .category = "test",
        .requiredWidth = 8,
        .requiredHeight = 8,
    };
    bool didAuthorWater_ = false;
};

struct WaterSourceRect {
    int xBeginInclusive = 0;
    int xEndExclusive = 0;
    int yBeginInclusive = 0;
    int yEndExclusive = 0;
};

WaterShapeMetrics computeWaterShapeMetrics(const WaterVolumeView& view)
{
    WaterShapeMetrics metrics{};
    metrics.centerX = computeVolumeCenterX(view);
    metrics.totalVolume = sumVolume(view);
    if (metrics.totalVolume <= 0.0f) {
        return metrics;
    }

    std::vector<float> activeColumnVolumes;
    std::vector<float> surfaceYs;
    int leftmostActiveX = -1;
    int rightmostActiveX = -1;
    float partialVolume = 0.0f;

    for (int x = 0; x < view.width; ++x) {
        float columnVolume = 0.0f;
        int firstOccupiedY = -1;

        for (int y = 0; y < view.height; ++y) {
            const float volume = view.volume[static_cast<size_t>(y) * view.width + x];
            columnVolume += volume;

            if (volume < kOccupiedVolumeThreshold) {
                continue;
            }

            ++metrics.occupiedCellCount;
            if (firstOccupiedY < 0) {
                firstOccupiedY = y;
            }

            if (volume < kNearlyFullVolumeThreshold) {
                ++metrics.partialCellCount;
                partialVolume += volume;
            }
        }

        if (firstOccupiedY < 0) {
            continue;
        }

        if (leftmostActiveX < 0) {
            leftmostActiveX = x;
        }
        rightmostActiveX = x;
        ++metrics.activeColumnCount;
        activeColumnVolumes.push_back(columnVolume);
        surfaceYs.push_back(static_cast<float>(firstOccupiedY));
    }

    metrics.averageFillPerOccupiedCell =
        metrics.totalVolume / static_cast<float>(metrics.occupiedCellCount);
    metrics.partialVolumeFraction = partialVolume / metrics.totalVolume;

    if (metrics.activeColumnCount > 0) {
        metrics.partialCellsPerActiveColumn = static_cast<float>(metrics.partialCellCount)
            / static_cast<float>(metrics.activeColumnCount);
        metrics.occupiedSpanWidth = rightmostActiveX - leftmostActiveX + 1;
    }

    if (!activeColumnVolumes.empty()) {
        const float columnMean =
            metrics.totalVolume / static_cast<float>(metrics.activeColumnCount);
        float columnVariance = 0.0f;
        for (const float columnVolume : activeColumnVolumes) {
            const float delta = columnVolume - columnMean;
            columnVariance += delta * delta;
        }
        columnVariance /= static_cast<float>(activeColumnVolumes.size());
        metrics.columnVolumeStdDev = std::sqrt(columnVariance);
    }

    if (!surfaceYs.empty()) {
        float surfaceMean = 0.0f;
        for (const float surfaceY : surfaceYs) {
            surfaceMean += surfaceY;
        }
        surfaceMean /= static_cast<float>(surfaceYs.size());

        float surfaceVariance = 0.0f;
        for (const float surfaceY : surfaceYs) {
            const float delta = surfaceY - surfaceMean;
            surfaceVariance += delta * delta;
        }
        surfaceVariance /= static_cast<float>(surfaceYs.size());
        metrics.surfaceYStdDev = std::sqrt(surfaceVariance);
    }

    const int bottomBandStart = std::max(0, view.height - 7);
    const int bottomBandEnd = std::max(0, view.height - 1);
    metrics.bottomSixRowsFraction =
        sumVolumeInRows(view, bottomBandStart, bottomBandEnd) / metrics.totalVolume;

    return metrics;
}

WaterColumnDimensions computeSandboxWaterColumnDimensions(int worldWidth, int worldHeight)
{
    return WaterColumnDimensions{
        .height = worldHeight / 3,
        .width = std::max(3, std::min(8, worldWidth / 20)),
    };
}

void setupEmptySandboxBasin(World& world, SandboxScenario& scenario)
{
    Config::Sandbox config{};
    config.quadrantEnabled = false;
    config.waterColumnEnabled = false;
    config.rightThrowEnabled = false;
    config.rainRate = 0.0;
    scenario.setConfig(config, world);
    world.setScenario(&scenario);
    scenario.setup(world);
}

WaterSourceRect computeNormalizedWaterSourceRect(int worldWidth, int worldHeight)
{
    constexpr int kBaselineWorldWidth = 47;
    constexpr int kBaselineWorldHeight = 30;
    constexpr int kBaselineSourceWidth = 3;
    constexpr int kBaselineSourceHeight = 10;

    const int sourceWidth = std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(worldWidth) * kBaselineSourceWidth / kBaselineWorldWidth)),
        1,
        std::max(1, worldWidth - 2));
    const int sourceHeight = std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(worldHeight) * kBaselineSourceHeight / kBaselineWorldHeight)),
        1,
        std::max(1, worldHeight - 1));

    return WaterSourceRect{
        .xBeginInclusive = 1,
        .xEndExclusive = std::min(worldWidth - 1, 1 + sourceWidth),
        .yBeginInclusive = 0,
        .yEndExclusive = sourceHeight,
    };
}

void refillWaterSourceRect(World& world, const WaterSourceRect& sourceRect)
{
    for (int y = sourceRect.yBeginInclusive; y < sourceRect.yEndExclusive; ++y) {
        for (int x = sourceRect.xBeginInclusive; x < sourceRect.xEndExclusive; ++x) {
            world.addBulkWaterAtCell(x, y, 1.0f);
        }
    }
}

void refillWaterSourceRect(MacProjectionWaterSim& sim, const WaterSourceRect& sourceRect)
{
    WaterVolumeMutableView volumeMutable{};
    if (!sim.tryGetMutableWaterVolumeView(volumeMutable)) {
        return;
    }

    for (int y = sourceRect.yBeginInclusive; y < sourceRect.yEndExclusive; ++y) {
        for (int x = sourceRect.xBeginInclusive; x < sourceRect.xEndExclusive; ++x) {
            const size_t idx = static_cast<size_t>(y) * volumeMutable.width + x;
            volumeMutable.volume[idx] = 1.0f;
        }
    }
}

WaterShapeMetrics sampleSandboxColumnShapeMetrics(int width, int height, double sampleTimeSeconds)
{
    constexpr double kDeltaTime = 0.016;

    World world(width, height);
    SandboxScenario scenario;
    Config::Sandbox config{};
    config.quadrantEnabled = false;
    config.waterColumnEnabled = true;
    config.rightThrowEnabled = false;
    config.rainRate = 0.0;
    scenario.setConfig(config, world);
    world.setScenario(&scenario);
    scenario.setup(world);

    double simTime = 0.0;
    while (simTime < sampleTimeSeconds - 0.5 * kDeltaTime) {
        world.advanceTime(kDeltaTime);
        simTime += kDeltaTime;
    }

    WaterVolumeView volumeView{};
    if (!world.tryGetWaterVolumeView(volumeView)) {
        return {};
    }

    return computeWaterShapeMetrics(volumeView);
}

void setupDirectEmptyBasin(World& world)
{
    WorldData& data = world.getData();
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y) = Cell();
        }
    }

    for (int x = 0; x < data.width; ++x) {
        data.at(x, data.height - 1).replaceMaterial(Material::EnumType::Wall, 1.0f);
    }
    for (int y = 0; y < data.height; ++y) {
        data.at(0, y).replaceMaterial(Material::EnumType::Wall, 1.0f);
        data.at(data.width - 1, y).replaceMaterial(Material::EnumType::Wall, 1.0f);
    }
}

WaterShapeMetrics sampleNormalizedBasinShapeMetrics(int width, int height, double sampleTimeSeconds)
{
    constexpr double kDeltaTime = 0.016;
    constexpr double kSourceDurationSeconds = 2.0;

    World world(width, height);
    SandboxScenario scenario;
    setupEmptySandboxBasin(world, scenario);

    const WaterSourceRect sourceRect = computeNormalizedWaterSourceRect(width, height);
    refillWaterSourceRect(world, sourceRect);

    double simTime = 0.0;
    while (simTime < sampleTimeSeconds - 0.5 * kDeltaTime) {
        world.advanceTime(kDeltaTime);
        simTime += kDeltaTime;

        if (simTime < kSourceDurationSeconds - 0.5 * kDeltaTime) {
            refillWaterSourceRect(world, sourceRect);
        }
    }

    WaterVolumeView volumeView{};
    if (!world.tryGetWaterVolumeView(volumeView)) {
        return {};
    }

    return computeWaterShapeMetrics(volumeView);
}

WaterShapeMetrics sampleNormalizedBasinShapeMetricsWithWorldPressureIterations(
    int width, int height, double sampleTimeSeconds, int pressureIterations)
{
    constexpr double kDeltaTime = 0.016;
    constexpr double kSourceDurationSeconds = 2.0;

    World world(width, height);
    SandboxScenario scenario;
    setupEmptySandboxBasin(world, scenario);
    world.getPhysicsSettings().mac_water_pressure_iterations = pressureIterations;

    const WaterSourceRect sourceRect = computeNormalizedWaterSourceRect(width, height);
    refillWaterSourceRect(world, sourceRect);

    double simTime = 0.0;
    while (simTime < sampleTimeSeconds - 0.5 * kDeltaTime) {
        world.advanceTime(kDeltaTime);
        simTime += kDeltaTime;

        if (simTime < kSourceDurationSeconds - 0.5 * kDeltaTime) {
            refillWaterSourceRect(world, sourceRect);
        }
    }

    WaterVolumeView volumeView{};
    if (!world.tryGetWaterVolumeView(volumeView)) {
        return {};
    }

    return computeWaterShapeMetrics(volumeView);
}

WaterShapeMetrics sampleNormalizedBasinShapeMetricsWithParameters(
    int width,
    int height,
    double sampleTimeSeconds,
    const MacProjectionWaterSim::Parameters& parameters)
{
    constexpr double kDeltaTime = 0.016;
    constexpr double kSourceDurationSeconds = 2.0;

    World world(width, height);
    setupDirectEmptyBasin(world);

    MacProjectionWaterSim sim;
    sim.setParametersForTesting(parameters);
    sim.resize(width, height);
    sim.reset();

    const WaterSourceRect sourceRect = computeNormalizedWaterSourceRect(width, height);
    refillWaterSourceRect(sim, sourceRect);

    double simTime = 0.0;
    while (simTime < sampleTimeSeconds - 0.5 * kDeltaTime) {
        sim.advanceTime(world, kDeltaTime);
        simTime += kDeltaTime;

        if (simTime < kSourceDurationSeconds - 0.5 * kDeltaTime) {
            refillWaterSourceRect(sim, sourceRect);
        }
    }

    WaterVolumeView volumeView{};
    if (!sim.tryGetWaterVolumeView(volumeView)) {
        return {};
    }

    return computeWaterShapeMetrics(volumeView);
}

std::string dumpSandboxColumnShapeMetrics()
{
    constexpr int kWidth = 47;
    constexpr int kHeight = 30;
    constexpr double kDurationSeconds = 12.0;
    constexpr double kSamplePeriodSeconds = 0.5;
    constexpr double kWaterColumnDurationSeconds = 2.0;

    World world(kWidth, kHeight);
    SandboxScenario scenario;
    Config::Sandbox config{};
    config.quadrantEnabled = false;
    config.waterColumnEnabled = true;
    config.rightThrowEnabled = false;
    config.rainRate = 0.0;
    scenario.setConfig(config, world);
    world.setScenario(&scenario);
    scenario.setup(world);

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "# occupied cell: volume >= " << kOccupiedVolumeThreshold << "\n";
    out << "# partial cell: " << kOccupiedVolumeThreshold << " <= volume < "
        << kNearlyFullVolumeThreshold << "\n";
    out << "time_s,source_on,total,center_x,occupied_cells,avg_fill,partial_cells,"
        << "partial_volume_frac,partial_cells_per_active_col,active_cols,span_cols,"
        << "column_volume_stddev,surface_y_stddev,bottom6_frac\n";

    auto appendMetrics = [&](double timeSeconds) {
        const WaterShapeMetrics metrics =
            sampleSandboxColumnShapeMetrics(kWidth, kHeight, timeSeconds);
        out << timeSeconds << "," << (timeSeconds < kWaterColumnDurationSeconds ? 1 : 0) << ","
            << metrics.totalVolume << "," << metrics.centerX << "," << metrics.occupiedCellCount
            << "," << metrics.averageFillPerOccupiedCell << "," << metrics.partialCellCount << ","
            << metrics.partialVolumeFraction << "," << metrics.partialCellsPerActiveColumn << ","
            << metrics.activeColumnCount << "," << metrics.occupiedSpanWidth << ","
            << metrics.columnVolumeStdDev << "," << metrics.surfaceYStdDev << ","
            << metrics.bottomSixRowsFraction << "\n";
    };

    for (double timeSeconds = 0.0; timeSeconds <= kDurationSeconds + 1e-9;
         timeSeconds += kSamplePeriodSeconds) {
        appendMetrics(timeSeconds);
    }

    return out.str();
}

std::string dumpSandboxColumnShapeSizeSweep()
{
    constexpr std::array<SandboxSizeCase, 4> kCases{ {
        SandboxSizeCase{ .height = 15, .width = 24 },
        SandboxSizeCase{ .height = 30, .width = 47 },
        SandboxSizeCase{ .height = 60, .width = 94 },
        SandboxSizeCase{ .height = 90, .width = 141 },
    } };
    constexpr double kSampleTimeSeconds = 10.0;

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "# sample_time_s=" << kSampleTimeSeconds << "\n";
    out << "width,height,source_w,source_h,total,center_x_frac,active_col_frac,span_frac,"
        << "full_cell_depth,avg_fill,partial_volume_frac,partial_cells_per_active_col,"
        << "column_volume_stddev,surface_y_stddev\n";

    for (const SandboxSizeCase& sizeCase : kCases) {
        const WaterShapeMetrics metrics =
            sampleSandboxColumnShapeMetrics(sizeCase.width, sizeCase.height, kSampleTimeSeconds);
        const WaterColumnDimensions sourceDims =
            computeSandboxWaterColumnDimensions(sizeCase.width, sizeCase.height);
        const float centerXFraction =
            sizeCase.width > 1 ? metrics.centerX / static_cast<float>(sizeCase.width - 1) : 0.0f;
        const float activeColumnFraction = sizeCase.width > 2
            ? static_cast<float>(metrics.activeColumnCount) / static_cast<float>(sizeCase.width - 2)
            : 0.0f;
        const float spanFraction = sizeCase.width > 2
            ? static_cast<float>(metrics.occupiedSpanWidth) / static_cast<float>(sizeCase.width - 2)
            : 0.0f;
        const float fullCellDepth = metrics.activeColumnCount > 0
            ? metrics.totalVolume / static_cast<float>(metrics.activeColumnCount)
            : 0.0f;

        out << sizeCase.width << "," << sizeCase.height << "," << sourceDims.width << ","
            << sourceDims.height << "," << metrics.totalVolume << "," << centerXFraction << ","
            << activeColumnFraction << "," << spanFraction << "," << fullCellDepth << ","
            << metrics.averageFillPerOccupiedCell << "," << metrics.partialVolumeFraction << ","
            << metrics.partialCellsPerActiveColumn << "," << metrics.columnVolumeStdDev << ","
            << metrics.surfaceYStdDev << "\n";
    }

    return out.str();
}

std::string dumpNormalizedBasinParameterSweep()
{
    struct FloatSweepCase {
        const char* label = "";
        float value = 0.0f;
    };

    struct IntSweepCase {
        const char* label = "";
        int value = 0;
    };

    constexpr std::array<SandboxSizeCase, 2> kSizes{ {
        SandboxSizeCase{ .height = 15, .width = 24 },
        SandboxSizeCase{ .height = 30, .width = 47 },
    } };
    constexpr std::array<FloatSweepCase, 3> kDampingCases{ {
        FloatSweepCase{ .label = "0.000000", .value = 0.0f },
        FloatSweepCase{ .label = "0.050000", .value = 0.05f },
        FloatSweepCase{ .label = "0.500000", .value = 0.50f },
    } };
    constexpr std::array<FloatSweepCase, 3> kSleepCases{ {
        FloatSweepCase{ .label = "0.000000", .value = 0.0f },
        FloatSweepCase{ .label = "0.000001", .value = 0.000001f },
        FloatSweepCase{ .label = "0.000500", .value = 0.000500f },
    } };
    constexpr std::array<IntSweepCase, 2> kPressureCases{ {
        IntSweepCase{ .label = "20", .value = 20 },
        IntSweepCase{ .label = "120", .value = 120 },
    } };
    constexpr double kSampleTimeSeconds = 10.0;

    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "# sample_time_s=" << kSampleTimeSeconds << "\n";
    out << "width,height,sweep,value,center_x_frac,avg_fill,partial_volume_frac,"
        << "partial_band_height_frac,column_volume_stddev_frac,surface_y_stddev_frac\n";

    const auto appendRow = [&](int width,
                               int height,
                               const char* sweepName,
                               const char* valueLabel,
                               const MacProjectionWaterSim::Parameters& parameters) {
        const WaterShapeMetrics metrics = sampleNormalizedBasinShapeMetricsWithParameters(
            width, height, kSampleTimeSeconds, parameters);
        const float centerXFraction =
            width > 1 ? metrics.centerX / static_cast<float>(width - 1) : 0.0f;
        const float partialBandHeightFraction = height > 1
            ? metrics.partialCellsPerActiveColumn / static_cast<float>(height - 1)
            : 0.0f;
        const float columnStdDevFraction =
            height > 1 ? metrics.columnVolumeStdDev / static_cast<float>(height - 1) : 0.0f;
        const float surfaceStdDevFraction =
            height > 1 ? metrics.surfaceYStdDev / static_cast<float>(height - 1) : 0.0f;

        out << width << "," << height << "," << sweepName << "," << valueLabel << ","
            << centerXFraction << "," << metrics.averageFillPerOccupiedCell << ","
            << metrics.partialVolumeFraction << "," << partialBandHeightFraction << ","
            << columnStdDevFraction << "," << surfaceStdDevFraction << "\n";
    };

    for (const SandboxSizeCase& sizeCase : kSizes) {
        const MacProjectionWaterSim::Parameters defaults{};
        appendRow(sizeCase.width, sizeCase.height, "baseline", "default", defaults);

        for (const FloatSweepCase& sweepCase : kDampingCases) {
            MacProjectionWaterSim::Parameters parameters = defaults;
            parameters.velocityDampingPerSecond = sweepCase.value;
            appendRow(sizeCase.width, sizeCase.height, "damping", sweepCase.label, parameters);
        }

        for (const FloatSweepCase& sweepCase : kSleepCases) {
            MacProjectionWaterSim::Parameters parameters = defaults;
            parameters.velocitySleepEpsilon = sweepCase.value;
            appendRow(
                sizeCase.width, sizeCase.height, "sleep_epsilon", sweepCase.label, parameters);
        }

        for (const IntSweepCase& sweepCase : kPressureCases) {
            MacProjectionWaterSim::Parameters parameters = defaults;
            parameters.pressureIterations = sweepCase.value;
            appendRow(
                sizeCase.width, sizeCase.height, "pressure_iters", sweepCase.label, parameters);
        }
    }

    return out.str();
}

std::string dumpNormalizedBasinResolutionSweep()
{
    constexpr std::array<SandboxSizeCase, 4> kCases{ {
        SandboxSizeCase{ .height = 15, .width = 24 },
        SandboxSizeCase{ .height = 30, .width = 47 },
        SandboxSizeCase{ .height = 60, .width = 94 },
        SandboxSizeCase{ .height = 90, .width = 141 },
    } };
    constexpr double kSampleTimeSeconds = 10.0;

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "# sample_time_s=" << kSampleTimeSeconds << "\n";
    out << "width,height,source_w,source_h,total_frac,center_x_frac,active_col_frac,span_frac,"
        << "full_depth_frac,avg_fill,partial_volume_frac,partial_band_height_frac,"
        << "column_volume_stddev_frac,surface_y_stddev_frac\n";

    for (const SandboxSizeCase& sizeCase : kCases) {
        const WaterShapeMetrics metrics =
            sampleNormalizedBasinShapeMetrics(sizeCase.width, sizeCase.height, kSampleTimeSeconds);
        const WaterSourceRect sourceRect =
            computeNormalizedWaterSourceRect(sizeCase.width, sizeCase.height);
        const int sourceWidth = sourceRect.xEndExclusive - sourceRect.xBeginInclusive;
        const int sourceHeight = sourceRect.yEndExclusive - sourceRect.yBeginInclusive;
        const float centerXFraction =
            sizeCase.width > 1 ? metrics.centerX / static_cast<float>(sizeCase.width - 1) : 0.0f;
        const float activeColumnFraction = sizeCase.width > 2
            ? static_cast<float>(metrics.activeColumnCount) / static_cast<float>(sizeCase.width - 2)
            : 0.0f;
        const float spanFraction = sizeCase.width > 2
            ? static_cast<float>(metrics.occupiedSpanWidth) / static_cast<float>(sizeCase.width - 2)
            : 0.0f;
        const float fullCellDepth = metrics.activeColumnCount > 0
            ? metrics.totalVolume / static_cast<float>(metrics.activeColumnCount)
            : 0.0f;
        const float totalFraction = sizeCase.width > 2 && sizeCase.height > 1
            ? metrics.totalVolume / static_cast<float>((sizeCase.width - 2) * (sizeCase.height - 1))
            : 0.0f;
        const float fullDepthFraction =
            sizeCase.height > 1 ? fullCellDepth / static_cast<float>(sizeCase.height - 1) : 0.0f;
        const float partialBandHeightFraction = sizeCase.height > 1
            ? metrics.partialCellsPerActiveColumn / static_cast<float>(sizeCase.height - 1)
            : 0.0f;
        const float columnStdDevFraction = sizeCase.height > 1
            ? metrics.columnVolumeStdDev / static_cast<float>(sizeCase.height - 1)
            : 0.0f;
        const float surfaceStdDevFraction = sizeCase.height > 1
            ? metrics.surfaceYStdDev / static_cast<float>(sizeCase.height - 1)
            : 0.0f;

        out << sizeCase.width << "," << sizeCase.height << "," << sourceWidth << "," << sourceHeight
            << "," << totalFraction << "," << centerXFraction << "," << activeColumnFraction << ","
            << spanFraction << "," << fullDepthFraction << "," << metrics.averageFillPerOccupiedCell
            << "," << metrics.partialVolumeFraction << "," << partialBandHeightFraction << ","
            << columnStdDevFraction << "," << surfaceStdDevFraction << "\n";
    }

    return out.str();
}

} // namespace

TEST(WaterMacStabilityTest, RestingPoolSettlesAndStopsMoving)
{
    constexpr int kWidth = 20;
    constexpr int kHeight = 20;
    constexpr double kDeltaTime = 0.02;
    constexpr int kWarmupSteps = 200;
    constexpr int kMeasureSteps = 200;

    World world(kWidth, kHeight);

    MacProjectionWaterSim sim;
    sim.resize(kWidth, kHeight);
    sim.reset();

    WaterVolumeMutableView volumeMutable{};
    ASSERT_TRUE(sim.tryGetMutableWaterVolumeView(volumeMutable));

    const int baseRows = 8;
    for (int y = kHeight - baseRows; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            volumeMutable.volume[static_cast<size_t>(y) * kWidth + x] = 1.0f;
        }
    }

    WaterVolumeView volumeView{};
    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float totalInitial = sumVolume(volumeView);
    ASSERT_NEAR(totalInitial, 160.0f, 0.001f);

    for (int step = 0; step < kWarmupSteps; ++step) {
        sim.advanceTime(world, kDeltaTime);
    }

    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    std::vector<float> prev(volumeView.volume.begin(), volumeView.volume.end());

    float maxStepL1Delta = 0.0f;
    float totalStepL1Delta = 0.0f;

    for (int step = 0; step < kMeasureSteps; ++step) {
        sim.advanceTime(world, kDeltaTime);

        ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
        float l1Delta = 0.0f;
        for (size_t i = 0; i < prev.size(); ++i) {
            const float cur = volumeView.volume[i];
            l1Delta += std::abs(cur - prev[i]);
            prev[i] = cur;
        }

        maxStepL1Delta = std::max(maxStepL1Delta, l1Delta);
        totalStepL1Delta += l1Delta;
    }

    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float totalFinal = sumVolume(volumeView);
    EXPECT_NEAR(totalFinal, totalInitial, 0.05f);

    const float avgStepL1Delta = totalStepL1Delta / static_cast<float>(kMeasureSteps);
    EXPECT_LT(maxStepL1Delta, 0.01f);
    EXPECT_LT(avgStepL1Delta, 0.001f);
}

TEST(WaterMacDisplacementTest, SolidCellDisplacesWaterAndConservesVolume)
{
    constexpr int kWidth = 5;
    constexpr int kHeight = 5;
    constexpr int kCenterX = 2;
    constexpr int kCenterY = 2;
    constexpr double kDeltaTime = 0.02;

    World world(kWidth, kHeight);
    world.getPhysicsSettings().gravity = 0.0;

    MacProjectionWaterSim sim;
    sim.resize(kWidth, kHeight);
    sim.reset();

    WaterVolumeMutableView volumeMutable{};
    ASSERT_TRUE(sim.tryGetMutableWaterVolumeView(volumeMutable));
    volumeMutable.volume[static_cast<size_t>(kCenterY) * kWidth + kCenterX] = 1.0f;

    WaterVolumeView volumeView{};
    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float totalInitial = sumVolume(volumeView);
    ASSERT_NEAR(totalInitial, 1.0f, 0.0001f);

    world.getData().at(kCenterX, kCenterY).replaceMaterial(Material::EnumType::Dirt, 1.0f);

    sim.advanceTime(world, kDeltaTime);

    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float totalFinal = sumVolume(volumeView);

    EXPECT_NEAR(
        volumeView.volume[static_cast<size_t>(kCenterY) * kWidth + kCenterX], 0.0f, 0.0001f);
    EXPECT_NEAR(
        volumeView.volume[static_cast<size_t>(kCenterY) * kWidth + (kCenterX - 1)], 1.0f, 0.0001f);
    EXPECT_NEAR(totalFinal, totalInitial, 0.0001f);
}

TEST(WaterMacStabilityTest, ResidualMistSettlesDownIntoTheBasin)
{
    constexpr int kWidth = 10;
    constexpr int kHeight = 10;
    constexpr int kSourceX = 5;
    constexpr double kDeltaTime = 0.016;
    constexpr float kResidualVolume = 0.00005f;
    constexpr int kResidualCells = 4;
    constexpr int kSteps = 12;

    World world(kWidth, kHeight);
    setupDirectEmptyBasin(world);

    MacProjectionWaterSim sim;
    sim.resize(kWidth, kHeight);
    sim.reset();

    WaterVolumeMutableView volumeMutable{};
    ASSERT_TRUE(sim.tryGetMutableWaterVolumeView(volumeMutable));
    for (int y = 0; y < kResidualCells; ++y) {
        volumeMutable.volume[static_cast<size_t>(y) * kWidth + kSourceX] = kResidualVolume;
    }

    WaterVolumeView volumeView{};
    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float totalInitial = sumVolume(volumeView);
    ASSERT_NEAR(totalInitial, kResidualVolume * kResidualCells, 1e-7f);

    for (int step = 0; step < kSteps; ++step) {
        sim.advanceTime(world, kDeltaTime);
    }

    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float totalFinal = sumVolume(volumeView);
    EXPECT_NEAR(totalFinal, totalInitial, 1e-7f);

    const int bottomPlayableY = kHeight - 2;
    const float aboveBottomRow = sumVolumeInRows(volumeView, 0, bottomPlayableY);
    const float bottomRow = sumVolumeInRows(volumeView, bottomPlayableY, bottomPlayableY + 1);

    EXPECT_LT(aboveBottomRow, 1e-7f);
    EXPECT_NEAR(bottomRow, totalInitial, 1e-7f);
}

TEST(WaterMacStabilityTest, ClockRainEventWritesBulkWaterWithoutLegacyWaterCellsInMacMode)
{
    constexpr int kWidth = 20;
    constexpr int kHeight = 10;

    World world(kWidth, kHeight);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    setupDirectEmptyBasin(world);

    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> uniformDist(0.0, 1.0);

    ClockEvents::updateRain(world, 1.0, rng, uniformDist);

    WaterVolumeView volumeView{};
    ASSERT_TRUE(world.tryGetWaterVolumeView(volumeView));
    EXPECT_NEAR(sumVolume(volumeView), 0.5f, 1e-6f);
    EXPECT_FALSE(hasLegacyWaterCells(world));
}

TEST(WaterMacStabilityTest, ScenarioTickWaterAuthoringFeedsSameFrameMacActivityAndTracker)
{
    World world(8, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    StartOfFrameWaterScenario scenario;
    scenario.setup(world);
    world.setScenario(&scenario);

    world.advanceTime(0.016);

    WaterVolumeView volumeView{};
    ASSERT_TRUE(world.tryGetWaterVolumeView(volumeView));
    EXPECT_GT(sumVolume(volumeView), 0.5f);

    WaterActivityView activityView{};
    ASSERT_TRUE(world.tryGetWaterActivityView(activityView));
    ASSERT_EQ(activityView.width, world.getData().width);
    ASSERT_EQ(activityView.height, world.getData().height);

    bool sawFluid = false;
    bool sawVolumeDelta = false;
    for (size_t idx = 0; idx < activityView.flags.size(); ++idx) {
        if (hasWaterActivityFlag(activityView.flags[idx], WaterActivityFlag::HasFluid)) {
            sawFluid = true;
        }
        if (idx < activityView.volume_delta.size() && activityView.volume_delta[idx] > 0.001f) {
            sawVolumeDelta = true;
        }
    }

    EXPECT_TRUE(sawFluid);
    EXPECT_TRUE(sawVolumeDelta);

    const auto& summary = world.getRegionActivityTracker().getRegionSummary(0, 0);
    EXPECT_TRUE(summary.has_mac_bulk_water);
    EXPECT_GT(summary.max_mac_water_volume_delta, 0.001f);
}

TEST(WaterMacStabilityTest, DamBreakScenarioSetupClearsPriorMacWaterAndUsesBulkWater)
{
    World world(6, 6);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setBulkWaterAmountAtCell(4, 4, 0.75f);

    DamBreakScenario scenario;
    scenario.setup(world);

    WaterVolumeView volumeView{};
    ASSERT_TRUE(world.tryGetWaterVolumeView(volumeView));
    EXPECT_NEAR(sumVolume(volumeView), 12.0f, 1e-6f);
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(4, 4), 0.0f, 1e-6f);
    EXPECT_FALSE(hasLegacyWaterCells(world));

    for (int y = 0; y < 6; ++y) {
        EXPECT_NEAR(world.getBulkWaterAmountAtCell(0, y), 1.0f, 1e-6f);
        EXPECT_NEAR(world.getBulkWaterAmountAtCell(1, y), 1.0f, 1e-6f);
        EXPECT_EQ(world.getData().at(2, y).material_type, Material::EnumType::Wall);
    }
}

TEST(WaterMacStabilityTest, ClockScenarioSetupClearsPriorMacWater)
{
    World world(50, 32);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setBulkWaterAmountAtCell(4, 4, 0.75f);

    ClockScenario scenario;
    scenario.setup(world);

    expectNoResidualMacWater(world);
}

TEST(WaterMacStabilityTest, EmptyScenarioSetupClearsPriorMacWater)
{
    World world(12, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setBulkWaterAmountAtCell(4, 4, 0.75f);

    EmptyScenario scenario;
    scenario.setup(world);

    expectNoResidualMacWater(world);
}

TEST(WaterMacStabilityTest, GooseTestScenarioSetupClearsPriorMacWater)
{
    World world(40, 30);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setBulkWaterAmountAtCell(4, 4, 0.75f);

    GooseTestScenario scenario;
    scenario.setup(world);

    expectNoResidualMacWater(world);
}

TEST(WaterMacStabilityTest, SandboxRainWritesBulkWaterWithoutLegacyWaterCellsInMacMode)
{
    World world(47, 30);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    SandboxScenario scenario;
    Config::Sandbox config{};
    config.quadrantEnabled = false;
    config.waterColumnEnabled = false;
    config.rightThrowEnabled = false;
    config.rainRate = 10.0;
    scenario.setConfig(config, world);
    world.setScenario(&scenario);
    scenario.setup(world);

    scenario.tick(world, 1.0);

    WaterVolumeView volumeView{};
    ASSERT_TRUE(world.tryGetWaterVolumeView(volumeView));
    EXPECT_GT(sumVolume(volumeView), 1.0f);
    EXPECT_FALSE(hasLegacyWaterCells(world));
}

TEST(WaterMacStabilityTest, TreeGerminationScenarioSetupClearsPriorMacWater)
{
    World world(32, 32);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setBulkWaterAmountAtCell(4, 4, 0.75f);

    GenomeRepository genomeRepository;
    TreeGerminationScenario scenario(genomeRepository);
    scenario.setup(world);

    expectNoResidualMacWater(world);
}

TEST(WaterMacStabilityTest, SandboxColumnFallsInsteadOfRemainingSuspended)
{
    constexpr int kWidth = 47;
    constexpr int kHeight = 30;
    constexpr double kDeltaTime = 0.016;
    constexpr int kSteps = 625; // 10 seconds at the regular sim timestep.

    World world(kWidth, kHeight);
    SandboxScenario scenario;
    Config::Sandbox config{};
    config.quadrantEnabled = false;
    config.waterColumnEnabled = true;
    config.rightThrowEnabled = false;
    config.rainRate = 0.0;
    scenario.setConfig(config, world);
    world.setScenario(&scenario);
    scenario.setup(world);

    for (int step = 0; step < kSteps; ++step) {
        world.advanceTime(kDeltaTime);
    }

    WaterVolumeView volumeView{};
    ASSERT_TRUE(world.tryGetWaterVolumeView(volumeView));
    const float totalVolume = sumVolume(volumeView);
    ASSERT_GT(totalVolume, 1.0f);

    const float upperHalfVolume = sumVolumeInRows(volumeView, 0, kHeight / 2);
    const float bottomSixRowsVolume = sumVolumeInRows(volumeView, kHeight - 7, kHeight - 1);
    const int suspendedCells = countCellsAtOrAbove(volumeView, 0, kHeight - 7, 0.05f);

    EXPECT_LT(upperHalfVolume, totalVolume * 0.08f);
    EXPECT_GT(bottomSixRowsVolume, totalVolume * 0.70f);
    EXPECT_LT(suspendedCells, 40);
}

TEST(WaterMacStabilityTest, SandboxColumnDoesNotRemainBiasedToSourceSideAfterTenSeconds)
{
    constexpr int kWidth = 47;
    constexpr int kHeight = 30;
    constexpr double kDeltaTime = 0.016;
    constexpr int kSteps = 625; // 10 seconds at the regular sim timestep.

    World world(kWidth, kHeight);
    SandboxScenario scenario;
    Config::Sandbox config{};
    config.quadrantEnabled = false;
    config.waterColumnEnabled = true;
    config.rightThrowEnabled = false;
    config.rainRate = 0.0;
    scenario.setConfig(config, world);
    world.setScenario(&scenario);
    scenario.setup(world);

    for (int step = 0; step < kSteps; ++step) {
        world.advanceTime(kDeltaTime);
    }

    WaterVolumeView volumeView{};
    ASSERT_TRUE(world.tryGetWaterVolumeView(volumeView));

    const float totalVolume = sumVolume(volumeView);
    ASSERT_GT(totalVolume, 1.0f);

    const int xMid = kWidth / 2;
    const float leftHalfVolume = sumVolumeInColumns(volumeView, 0, xMid);
    const float rightHalfVolume = sumVolumeInColumns(volumeView, xMid, kWidth);
    const float centerX = computeVolumeCenterX(volumeView);

    EXPECT_LE(leftHalfVolume, rightHalfVolume + totalVolume * 0.10f);
    EXPECT_GE(centerX, static_cast<float>(kWidth - 1) * 0.45f);
}

TEST(WaterMacStabilityTest, WorldManagedPressureIterationsSettingChangesNormalizedBasinShape)
{
    constexpr int kWidth = 47;
    constexpr int kHeight = 30;
    constexpr double kSampleTimeSeconds = 10.0;

    const WaterShapeMetrics baseline = sampleNormalizedBasinShapeMetricsWithWorldPressureIterations(
        kWidth, kHeight, kSampleTimeSeconds, 60);
    const WaterShapeMetrics reducedPressureIterations =
        sampleNormalizedBasinShapeMetricsWithWorldPressureIterations(
            kWidth, kHeight, kSampleTimeSeconds, 20);

    EXPECT_GT(
        reducedPressureIterations.averageFillPerOccupiedCell,
        baseline.averageFillPerOccupiedCell + 0.08f);
    EXPECT_LT(
        reducedPressureIterations.partialVolumeFraction, baseline.partialVolumeFraction - 0.20f);
}

TEST(WaterMacStabilityTest, SyncToSettingsAppliesMacRuntimeParameters)
{
    PhysicsSettings settings = getDefaultPhysicsSettings();
    settings.mac_water_pressure_iterations = 17;
    settings.mac_water_velocity_damping_per_second = 0.35;
    settings.mac_water_velocity_sleep_epsilon = 0.0002;

    MacProjectionWaterSim sim;
    sim.syncToSettings(settings);

    const MacProjectionWaterSim::Parameters& parameters = sim.getParametersForTesting();
    EXPECT_EQ(parameters.pressureIterations, 17);
    EXPECT_FLOAT_EQ(parameters.velocityDampingPerSecond, 0.35f);
    EXPECT_FLOAT_EQ(parameters.velocitySleepEpsilon, 0.0002f);
}

TEST(WaterMacStabilityTest, DISABLED_SandboxColumnShapeDiagnostics)
{
    std::cout << dumpSandboxColumnShapeMetrics();
}

TEST(WaterMacStabilityTest, DISABLED_SandboxColumnShapeSizeSweepDiagnostics)
{
    std::cout << dumpSandboxColumnShapeSizeSweep();
}

TEST(WaterMacStabilityTest, DISABLED_NormalizedBasinResolutionSweepDiagnostics)
{
    std::cout << dumpNormalizedBasinResolutionSweep();
}

TEST(WaterMacStabilityTest, DISABLED_NormalizedBasinParameterSweepDiagnostics)
{
    std::cout << dumpNormalizedBasinParameterSweep();
}
