#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/water/MacProjectionWaterSim.h"

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using namespace DirtSim;

namespace {

float sumVolume(const WaterVolumeView& view)
{
    float total = 0.0f;
    for (float v : view.volume) {
        total += v;
    }
    return total;
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
