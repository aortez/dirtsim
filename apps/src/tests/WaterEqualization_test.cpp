#include "core/World.h"
#include "core/WorldData.h"
#include "core/water/MacProjectionWaterSim.h"

#include <cmath>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

float sumColumnVolume(const WaterVolumeView& view, int x)
{
    float total = 0.0f;
    for (int y = 0; y < view.height; ++y) {
        total += view.volume[static_cast<size_t>(y) * view.width + x];
    }
    return total;
}

struct SideVolumes {
    float left = 0.0f;
    float wall = 0.0f;
    float right = 0.0f;
    float total = 0.0f;
};

SideVolumes sumVolumesByWall(const WaterVolumeView& view, int wallX)
{
    SideVolumes sums{};
    for (int y = 0; y < view.height; ++y) {
        for (int x = 0; x < view.width; ++x) {
            const float v = view.volume[static_cast<size_t>(y) * view.width + x];
            sums.total += v;
            if (x < wallX) {
                sums.left += v;
            }
            else if (x > wallX) {
                sums.right += v;
            }
            else {
                sums.wall += v;
            }
        }
    }
    return sums;
}

} // namespace

/**
 * @brief Test that MAC-projected water volume equalizes through a bottom opening.
 */
TEST(WaterEqualizationTest, WaterFlowsThroughOpening)
{
    World world(3, 6);
    WorldData& data = world.getData();

    for (int y = 0; y < 5; ++y) {
        data.at(1, y).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    MacProjectionWaterSim sim;
    sim.resize(data.width, data.height);
    sim.reset();

    WaterVolumeMutableView volumeMutable{};
    ASSERT_TRUE(sim.tryGetMutableWaterVolumeView(volumeMutable));
    for (int y = 1; y < volumeMutable.height; ++y) {
        volumeMutable.volume[static_cast<size_t>(y) * volumeMutable.width] = 1.0f;
    }

    WaterVolumeView volumeView{};
    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float leftInitial = sumColumnVolume(volumeView, 0);
    const float rightInitial = sumColumnVolume(volumeView, 2);
    EXPECT_NEAR(leftInitial, 5.0f, 0.001f);
    EXPECT_NEAR(rightInitial, 0.0f, 0.001f);

    const double deltaTime = 0.02;
    constexpr int kSteps = 600;
    for (int step = 0; step < kSteps; ++step) {
        sim.advanceTime(world, deltaTime);
    }

    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float leftFinal = sumColumnVolume(volumeView, 0);
    const float rightFinal = sumColumnVolume(volumeView, 2);
    const float middleFinal = sumColumnVolume(volumeView, 1);
    const float totalFinal = leftFinal + middleFinal + rightFinal;

    EXPECT_GT(rightFinal, 0.5f);
    EXPECT_LT(leftFinal, leftInitial);
    EXPECT_NEAR(totalFinal, leftInitial, 0.01f);
}

/**
 * @brief Test that MAC-projected water volume transfers through a narrow wall gap at larger scale.
 */
TEST(WaterEqualizationTest, WaterFlowsThroughOpeningLargeWorld)
{
    constexpr int kWidth = 51;
    constexpr int kHeight = 51;
    constexpr int kWallX = kWidth / 2;
    constexpr int kGapHeight = 2;

    World world(kWidth, kHeight);
    WorldData& data = world.getData();

    for (int y = 0; y < kHeight - kGapHeight; ++y) {
        data.at(kWallX, y).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    MacProjectionWaterSim sim;
    sim.resize(kWidth, kHeight);
    sim.reset();

    WaterVolumeMutableView volumeMutable{};
    ASSERT_TRUE(sim.tryGetMutableWaterVolumeView(volumeMutable));

    const int fillStartY = (kHeight / 2) + 1;
    for (int y = fillStartY; y < kHeight; ++y) {
        for (int x = kWallX + 1; x < kWidth; ++x) {
            volumeMutable.volume[static_cast<size_t>(y) * kWidth + x] = 1.0f;
        }
    }

    WaterVolumeView volumeView{};
    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const SideVolumes initialSums = sumVolumesByWall(volumeView, kWallX);
    ASSERT_NEAR(initialSums.total, 625.0f, 0.001f);
    ASSERT_NEAR(initialSums.left, 0.0f, 0.001f);

    const double deltaTime = 0.02;
    constexpr int kSteps = 1000;
    for (int step = 0; step < kSteps; ++step) {
        sim.advanceTime(world, deltaTime);
        if (step == 199) {
            ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
            const SideVolumes checkpoint = sumVolumesByWall(volumeView, kWallX);
            EXPECT_GT(checkpoint.left, 2.0f);
            EXPECT_NEAR(checkpoint.total, initialSums.total, 0.02f);
        }
        if (step == 399) {
            ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
            const SideVolumes checkpoint = sumVolumesByWall(volumeView, kWallX);
            EXPECT_GT(checkpoint.left, initialSums.total * 0.05f);
            EXPECT_NEAR(checkpoint.total, initialSums.total, 0.02f);
        }
    }

    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const SideVolumes finalSums = sumVolumesByWall(volumeView, kWallX);

    EXPECT_NEAR(finalSums.total, initialSums.total, 0.05f);
    EXPECT_GT(finalSums.left, initialSums.total * 0.075f);
}
