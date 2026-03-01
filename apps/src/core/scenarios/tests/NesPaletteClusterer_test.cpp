#include "core/organisms/DuckSensoryData.h"
#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"
#include "core/scenarios/nes/NesPaletteFrame.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <set>
#include <vector>

using namespace DirtSim;

TEST(NesPaletteClustererTest, FallbackMappingAlwaysReturnsValidChannel)
{
    NesPaletteClusterer clusterer;

    for (uint8_t i = 0; i < 64; ++i) {
        const uint8_t mapped = clusterer.mapIndex(i);
        EXPECT_LT(mapped, static_cast<uint8_t>(DuckSensoryData::NUM_MATERIALS));
    }
}

TEST(NesPaletteClustererTest, ObserveFramesBuildsStableClustersForTenDistinctIndices)
{
    NesPaletteClusterer clusterer;
    clusterer.reset("unit-test-rom");

    constexpr uint16_t width = 10;
    constexpr uint16_t height = 10;
    constexpr std::array<uint8_t, 10> kIndices = { 0, 6, 12, 18, 24, 30, 36, 42, 48, 54 };

    NesPaletteFrame frame;
    frame.width = width;
    frame.height = height;
    frame.indices.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < frame.indices.size(); ++i) {
        frame.indices[i] = kIndices[i % kIndices.size()];
    }

    for (uint64_t i = 0; i < 60; ++i) {
        frame.frameId = i;
        clusterer.observeFrame(frame);
    }

    EXPECT_TRUE(clusterer.isReady());

    std::set<uint8_t> mapped;
    for (uint8_t idx : kIndices) {
        mapped.insert(clusterer.mapIndex(idx));
    }
    EXPECT_EQ(mapped.size(), kIndices.size());
}

TEST(NesDuckSensoryBuilderTest, DownsampleMapsPaletteIndicesIntoMaterialHistogramChannels)
{
    NesPaletteClusterer clusterer;

    constexpr uint16_t width = 15;
    constexpr uint16_t height = 15;
    NesPaletteFrame frame;
    frame.width = width;
    frame.height = height;
    frame.frameId = 1;
    frame.indices.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    constexpr uint8_t kLeftIndex = 0;
    constexpr uint8_t kRightIndex = 63;
    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            const bool left = x < 7;
            frame.indices[static_cast<size_t>(y) * width + x] = left ? kLeftIndex : kRightIndex;
        }
    }

    const uint8_t leftChannel = clusterer.mapIndex(kLeftIndex);
    const uint8_t rightChannel = clusterer.mapIndex(kRightIndex);
    ASSERT_NE(leftChannel, rightChannel);

    const DuckSensoryData sensory =
        makeNesDuckSensoryDataFromPaletteFrame(clusterer, frame, 1.0 / 60.0);

    const auto& leftHistogram = sensory.material_histograms[0][0];
    EXPECT_DOUBLE_EQ(leftHistogram[static_cast<size_t>(leftChannel)], 1.0);

    const auto& rightHistogram = sensory.material_histograms[0][14];
    EXPECT_DOUBLE_EQ(rightHistogram[static_cast<size_t>(rightChannel)], 1.0);
}
