#include "core/World.h"
#include "core/WorldData.h"
#include "core/water/MacProjectionWaterSim.h"

#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <string>

using namespace DirtSim;

namespace {

char volumeToChar(float v)
{
    if (v <= 0.0001f) {
        return ' ';
    }
    if (v < 0.05f) {
        return '.';
    }
    if (v < 0.15f) {
        return ':';
    }
    if (v < 0.30f) {
        return '-';
    }
    if (v < 0.45f) {
        return '=';
    }
    if (v < 0.60f) {
        return '+';
    }
    if (v < 0.75f) {
        return '*';
    }
    if (v < 0.90f) {
        return '#';
    }
    return '@';
}

float sumHalfVolume(const WaterVolumeView& view, bool rightHalf)
{
    const int xStart = rightHalf ? view.width / 2 : 0;
    const int xEnd = rightHalf ? view.width : view.width / 2;

    float total = 0.0f;
    for (int y = 0; y < view.height; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            total += view.volume[static_cast<size_t>(y) * view.width + x];
        }
    }
    return total;
}

bool isWaterDumpEnabled()
{
    const char* env = std::getenv("DIRTSIM_MAC_WATER_DUMP");
    if (env == nullptr) {
        return false;
    }
    return env[0] != '\0' && env[0] != '0';
}

void dumpWaterVolume(const WaterVolumeView& view, int step)
{
    const float left = sumHalfVolume(view, false);
    const float right = sumHalfVolume(view, true);
    const float total = left + right;

    std::cerr << "\n=== WaterVolume step " << step << " ===\n";
    std::cerr << "total=" << total << " left=" << left << " right=" << right << "\n";

    const int xMid = view.width / 2;
    for (int y = 0; y < view.height; ++y) {
        std::string line;
        line.reserve(static_cast<size_t>(view.width) + 1);

        for (int x = 0; x < view.width; ++x) {
            if (x == xMid) {
                line.push_back('|');
            }
            line.push_back(volumeToChar(view.volume[static_cast<size_t>(y) * view.width + x]));
        }

        std::cerr << line << "\n";
    }
}

} // namespace

TEST(WaterMacQuadrantEqualizationTest, FilledQuadrantSpreadsAcrossWorld)
{
    constexpr int kWidth = 50;
    constexpr int kHeight = 50;
    constexpr int kSteps = 1000;
    constexpr double kDeltaTime = 0.02;

    World world(kWidth, kHeight);

    MacProjectionWaterSim sim;
    sim.resize(kWidth, kHeight);
    sim.reset();

    WaterVolumeMutableView volumeMutable{};
    ASSERT_TRUE(sim.tryGetMutableWaterVolumeView(volumeMutable));

    const int xMid = kWidth / 2;
    const int yMid = kHeight / 2;
    for (int y = yMid; y < kHeight; ++y) {
        for (int x = xMid; x < kWidth; ++x) {
            volumeMutable.volume[static_cast<size_t>(y) * kWidth + x] = 1.0f;
        }
    }

    WaterVolumeView volumeView{};
    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float totalInitial = sumHalfVolume(volumeView, false) + sumHalfVolume(volumeView, true);
    ASSERT_NEAR(totalInitial, 625.0f, 0.001f);
    ASSERT_NEAR(sumHalfVolume(volumeView, false), 0.0f, 0.001f);

    const bool enableDump = isWaterDumpEnabled();
    if (enableDump) {
        dumpWaterVolume(volumeView, 0);
    }

    for (int step = 0; step < kSteps; ++step) {
        sim.advanceTime(world, kDeltaTime);
        const int stepNumber = step + 1;
        if (enableDump && stepNumber % 100 == 0) {
            ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
            dumpWaterVolume(volumeView, stepNumber);
        }
    }

    ASSERT_TRUE(sim.tryGetWaterVolumeView(volumeView));
    const float leftFinal = sumHalfVolume(volumeView, false);
    const float rightFinal = sumHalfVolume(volumeView, true);
    const float totalFinal = leftFinal + rightFinal;

    EXPECT_NEAR(totalFinal, totalInitial, 0.05f);
    EXPECT_GT(leftFinal, totalInitial * 0.35f);
    EXPECT_LT(std::abs(leftFinal - rightFinal), totalInitial * 0.20f);
}
