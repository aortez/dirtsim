#include "core/scenarios/nes/NesTileSensoryBuilder.h"

#include "core/organisms/evolution/NesPolicyLayout.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

using namespace DirtSim;

namespace {

size_t relativeTileIndex(uint16_t column, uint16_t row)
{
    return static_cast<size_t>(row) * NesPlayerRelativeTileFrame::RelativeTileColumns + column;
}

size_t screenTileIndex(uint16_t column, uint16_t row)
{
    return static_cast<size_t>(row) * NesTileFrame::VisibleTileColumns + column;
}

int16_t pixelForTile(uint16_t tile)
{
    return static_cast<int16_t>(tile * NesPlayerRelativeTileFrame::TileSizePixels + 4u);
}

NesTileFrame makeTileFrame(uint64_t frameId, uint16_t scrollX, uint16_t scrollY, uint64_t fillHash)
{
    NesTileFrame frame{
        .frameId = frameId,
        .scrollX = scrollX,
        .scrollY = scrollY,
    };
    frame.tilePatternHashes.fill(fillHash);
    return frame;
}

void setSolidTilePattern(NesPpuSnapshot& snapshot, uint8_t tileId, uint8_t color)
{
    const size_t base = static_cast<size_t>(tileId) * 16u;
    const uint8_t lo = (color & 0x01u) != 0u ? 0xFFu : 0x00u;
    const uint8_t hi = (color & 0x02u) != 0u ? 0xFFu : 0x00u;
    for (size_t row = 0; row < 8u; ++row) {
        snapshot.chr[base + row] = lo;
        snapshot.chr[base + 8u + row] = hi;
    }
}

} // namespace

TEST(NesTileSensoryBuilderTest, BuildsPlayerRelativeSensoryFromTileFrame)
{
    NesTileTokenizer tokenizer;
    const auto buildResult =
        tokenizer.buildVocabulary(std::vector<NesTileTokenizer::TilePatternHash>{ 30u, 10u, 20u });
    ASSERT_TRUE(buildResult.isValue()) << buildResult.errorValue();
    tokenizer.freeze();

    NesTileFrame tileFrame = makeTileFrame(123u, 12u, 96u, 20u);
    tileFrame.tilePatternHashes[screenTileIndex(0u, 0u)] = 10u;
    tileFrame.tilePatternHashes[screenTileIndex(16u, 20u)] = 30u;

    NesTileSensoryBuilderInput input{
        .playerScreenX = pixelForTile(16u),
        .playerScreenY = pixelForTile(20u),
        .facingX = -1.0f,
        .selfViewX = 0.25f,
        .selfViewY = 0.75f,
        .controllerMask =
            NesPolicyLayout::ButtonRight | NesPolicyLayout::ButtonDown | NesPolicyLayout::ButtonA,
        .energy = 0.5f,
        .health = 0.25f,
        .deltaTimeSeconds = 1.0 / 60.0,
    };
    input.specialSenses[0u] = 0.125;
    input.specialSenses[17u] = -1.0;

    const auto sensoryResult = makeNesTileSensoryDataFromTileFrame(tileFrame, tokenizer, input);

    ASSERT_TRUE(sensoryResult.isValue()) << sensoryResult.errorValue();
    const NesTileSensoryData& sensory = sensoryResult.value();
    EXPECT_EQ(sensory.tileFrame.frameId, 123u);
    EXPECT_EQ(sensory.tileFrame.scrollX, 12u);
    EXPECT_EQ(sensory.tileFrame.scrollY, 96u);
    EXPECT_EQ(sensory.tileFrame.playerTileColumn, 16);
    EXPECT_EQ(sensory.tileFrame.playerTileRow, 20);
    EXPECT_EQ(
        sensory.tileFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        3u);
    EXPECT_EQ(sensory.tileFrame.tokens[relativeTileIndex(15u, 7u)], 1u);
    EXPECT_EQ(sensory.tileFrame.tokens[relativeTileIndex(46u, 34u)], 2u);
    EXPECT_EQ(sensory.facingX, -1.0f);
    EXPECT_EQ(sensory.selfViewX, 0.25f);
    EXPECT_EQ(sensory.selfViewY, 0.75f);
    EXPECT_EQ(sensory.previousControlX, 1.0f);
    EXPECT_EQ(sensory.previousControlY, 1.0f);
    EXPECT_TRUE(sensory.previousA);
    EXPECT_FALSE(sensory.previousB);
    EXPECT_EQ(sensory.specialSenses[0u], 0.125);
    EXPECT_EQ(sensory.specialSenses[17u], -1.0);
    EXPECT_EQ(sensory.energy, 0.5f);
    EXPECT_EQ(sensory.health, 0.25f);
    EXPECT_DOUBLE_EQ(sensory.deltaTimeSeconds, 1.0 / 60.0);
}

TEST(NesTileSensoryBuilderTest, FrozenUnknownHashFailsFromTileFrame)
{
    NesTileTokenizer tokenizer;
    const auto buildResult =
        tokenizer.buildVocabulary(std::vector<NesTileTokenizer::TilePatternHash>{ 10u });
    ASSERT_TRUE(buildResult.isValue()) << buildResult.errorValue();
    tokenizer.freeze();

    NesTileFrame tileFrame = makeTileFrame(123u, 0u, 0u, 10u);
    tileFrame.tilePatternHashes[5u] = 20u;

    const auto sensoryResult =
        makeNesTileSensoryDataFromTileFrame(tileFrame, tokenizer, NesTileSensoryBuilderInput{});

    ASSERT_TRUE(sensoryResult.isError());
    EXPECT_NE(sensoryResult.errorValue().find("Failed to tokenize tile frame"), std::string::npos);
    EXPECT_NE(sensoryResult.errorValue().find("cell 5"), std::string::npos);
    EXPECT_NE(sensoryResult.errorValue().find("Frozen vocabulary missing"), std::string::npos);
}

TEST(NesTileSensoryBuilderTest, BuildsFromPpuSnapshot)
{
    NesPpuSnapshot snapshot;
    snapshot.frameId = 7u;
    snapshot.mirror = 2u;
    setSolidTilePattern(snapshot, 1u, 1u);
    snapshot.vram[32u] = 1u;

    const NesTileFrame expectedTileFrame = makeNesTileFrame(snapshot);
    NesTileTokenizer tokenizer;
    const auto buildResult = tokenizer.buildVocabulary(
        std::vector<NesTileTokenizer::TilePatternHash>(
            expectedTileFrame.tilePatternHashes.begin(),
            expectedTileFrame.tilePatternHashes.end()));
    ASSERT_TRUE(buildResult.isValue()) << buildResult.errorValue();
    tokenizer.freeze();
    const auto expectedTokenResult =
        tokenizer.tokenForHash(expectedTileFrame.tilePatternHashes[screenTileIndex(0u, 0u)]);
    ASSERT_TRUE(expectedTokenResult.isValue()) << expectedTokenResult.errorValue();

    const auto sensoryResult = makeNesTileSensoryDataFromPpuSnapshot(
        snapshot,
        tokenizer,
        NesTileSensoryBuilderInput{
            .playerScreenX = pixelForTile(0u),
            .playerScreenY = pixelForTile(0u),
        });

    ASSERT_TRUE(sensoryResult.isValue()) << sensoryResult.errorValue();
    const NesTileSensoryData& sensory = sensoryResult.value();
    EXPECT_EQ(sensory.tileFrame.frameId, 7u);
    EXPECT_EQ(
        sensory.tileFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        expectedTokenResult.value());
}
