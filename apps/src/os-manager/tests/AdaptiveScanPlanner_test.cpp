#include "os-manager/network/AdaptiveScanPlanner.h"
#include <array>
#include <chrono>
#include <gtest/gtest.h>

using namespace DirtSim::OsManager;

namespace {

constexpr int kWidth20Mhz = 20;
constexpr int kWidth40Mhz = 40;
constexpr int kWidth80Mhz = 80;

ScanStep stepForChannel(const int channel)
{
    return ScanStep{
        .tuning =
            ScannerTuning{
                .band = scannerBandFromChannel(channel).value(),
                .primaryChannel = channel,
                .widthMhz = kWidth20Mhz,
                .centerChannel = std::nullopt,
            },
        .dwellMs = 150,
    };
}

ScanStep stepForTuning(const ScannerTuning& tuning)
{
    return ScanStep{
        .tuning = tuning,
        .dwellMs = 150,
    };
}

AdaptiveScanPlannerConfig deterministicPlannerConfig()
{
    return AdaptiveScanPlannerConfig{
        .trackingDwellMs = 150,
        .discoveryDwellMs = 250,
        .minDwellMs = 100,
        .maxDwellMs = 300,
        .trackingStepsPerDiscovery = 3,
        .trackingJitterMs = 0,
        .discoveryJitterMs = 0,
        .maxRevisitAgeMs = 3000,
        .rngSeed = 1,
    };
}

TEST(AdaptiveScanPlannerTest, DiscoveryAdvancesTrackingCursor)
{
    AdaptiveScanPlanner planner(deterministicPlannerConfig());
    planner.setFocus(ScannerBand::Band5Ghz, kWidth20Mhz);

    const auto start = std::chrono::steady_clock::time_point{};
    const auto first = planner.nextStep(start);
    ASSERT_EQ(first.tuning.primaryChannel, 36);

    planner.recordObservation(
        StepObservation{
            .step = first,
            .sawTraffic = false,
            .radiosSeen = 0,
            .newRadiosSeen = 0,
            .strongestSignalDbm = std::nullopt,
            .observedChannels = {},
        },
        start);

    const auto second = planner.nextStep(start + std::chrono::milliseconds(1));
    EXPECT_EQ(second.tuning.primaryChannel, 40);
}

TEST(AdaptiveScanPlannerTest, FocusSwitchReturnsChannelsFromNewBand)
{
    AdaptiveScanPlanner planner(deterministicPlannerConfig());
    planner.setFocus(ScannerBand::Band24Ghz, kWidth20Mhz);

    const auto start = std::chrono::steady_clock::time_point{};
    const auto first = planner.nextStep(start);
    EXPECT_EQ(first.tuning.band, ScannerBand::Band24Ghz);
    EXPECT_GE(first.tuning.primaryChannel, 1);
    EXPECT_LE(first.tuning.primaryChannel, 11);

    planner.setFocus(ScannerBand::Band5Ghz, kWidth20Mhz);
    const auto second = planner.nextStep(start + std::chrono::milliseconds(1));
    EXPECT_EQ(second.tuning.band, ScannerBand::Band5Ghz);
    EXPECT_GE(second.tuning.primaryChannel, 36);
}

TEST(AdaptiveScanPlannerTest, FocusWidthReturnsTuningsFromRequested5GHzWidth)
{
    AdaptiveScanPlanner planner(deterministicPlannerConfig());

    planner.setFocus(ScannerBand::Band5Ghz, kWidth80Mhz);
    const auto start = std::chrono::steady_clock::time_point{};
    const auto first = planner.nextStep(start);
    EXPECT_EQ(first.tuning.band, ScannerBand::Band5Ghz);
    EXPECT_EQ(first.tuning.widthMhz, kWidth80Mhz);
    ASSERT_TRUE(first.tuning.centerChannel.has_value());
    EXPECT_TRUE(
        first.tuning.centerChannel.value() == 42 || first.tuning.centerChannel.value() == 155);

    planner.recordObservation(
        StepObservation{
            .step = stepForTuning(first.tuning),
            .sawTraffic = false,
            .radiosSeen = 0,
            .newRadiosSeen = 0,
            .strongestSignalDbm = std::nullopt,
            .observedChannels = {},
        },
        start);

    planner.setFocus(ScannerBand::Band5Ghz, kWidth40Mhz);
    const auto second = planner.nextStep(start + std::chrono::milliseconds(1));
    EXPECT_EQ(second.tuning.band, ScannerBand::Band5Ghz);
    EXPECT_EQ(second.tuning.widthMhz, kWidth40Mhz);
    ASSERT_TRUE(second.tuning.centerChannel.has_value());
    EXPECT_TRUE(
        second.tuning.centerChannel.value() == 38 || second.tuning.centerChannel.value() == 46
        || second.tuning.centerChannel.value() == 151
        || second.tuning.centerChannel.value() == 159);
}

TEST(AdaptiveScanPlannerTest, DiscoveryPrioritizesOldestVisitedChannelOverRecentQuietChannel)
{
    AdaptiveScanPlanner planner(deterministicPlannerConfig());
    planner.setFocus(ScannerBand::Band5Ghz, kWidth20Mhz);

    const std::array<int, 9> channels{ { 36, 40, 44, 48, 149, 153, 157, 161, 165 } };
    auto now = std::chrono::steady_clock::time_point{};
    for (const int channel : channels) {
        planner.recordObservation(
            StepObservation{
                .step = stepForChannel(channel),
                .sawTraffic = channel != 165,
                .radiosSeen = channel != 165 ? 1u : 0u,
                .newRadiosSeen = channel != 165 ? 1u : 0u,
                .strongestSignalDbm = channel != 165 ? std::optional<int>(-60) : std::nullopt,
                .observedChannels =
                    channel != 165 ? std::vector<int>{ channel } : std::vector<int>{},
            },
            now);
        now += std::chrono::milliseconds(100);
    }

    const auto next = planner.nextStep(now + std::chrono::milliseconds(4000));
    EXPECT_EQ(next.tuning.primaryChannel, 36);
}

} // namespace
