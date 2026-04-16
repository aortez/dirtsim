#include "core/scenarios/nes/NesTileVocabularyBuilder.h"

#include "core/scenarios/nes/NesTileFrame.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

using namespace DirtSim;

namespace {

NesTileFrame makeTileFrame(std::vector<uint64_t> hashes)
{
    NesTileFrame frame;
    frame.tilePatternHashes.fill(10u);
    for (size_t i = 0; i < hashes.size() && i < frame.tilePatternHashes.size(); ++i) {
        frame.tilePatternHashes[i] = hashes[i];
    }
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

size_t visibleTileCount()
{
    return static_cast<size_t>(NesTileFrame::VisibleTileColumns) * NesTileFrame::VisibleTileRows;
}

} // namespace

TEST(NesTileVocabularyBuilderTest, BuildsDeterministicTokensRegardlessOfSampleOrder)
{
    NesTileVocabularyBuilder firstBuilder;
    firstBuilder.addFrame(makeTileFrame({ 30u, 10u }));
    firstBuilder.addFrame(makeTileFrame({ 20u, 10u }));

    NesTileVocabularyBuilder secondBuilder;
    secondBuilder.addFrame(makeTileFrame({ 20u, 10u }));
    secondBuilder.addFrame(makeTileFrame({ 30u, 10u }));

    auto firstTokenizerResult = firstBuilder.buildFrozenTokenizer();
    auto secondTokenizerResult = secondBuilder.buildFrozenTokenizer();

    ASSERT_TRUE(firstTokenizerResult.isValue()) << firstTokenizerResult.errorValue();
    ASSERT_TRUE(secondTokenizerResult.isValue()) << secondTokenizerResult.errorValue();
    NesTileTokenizer& firstTokenizer = firstTokenizerResult.value();
    NesTileTokenizer& secondTokenizer = secondTokenizerResult.value();
    EXPECT_EQ(firstTokenizer.getMode(), NesTileTokenizer::Mode::Frozen);
    EXPECT_EQ(secondTokenizer.getMode(), NesTileTokenizer::Mode::Frozen);
    EXPECT_EQ(firstTokenizer.tokenForHash(10u).value(), secondTokenizer.tokenForHash(10u).value());
    EXPECT_EQ(firstTokenizer.tokenForHash(20u).value(), secondTokenizer.tokenForHash(20u).value());
    EXPECT_EQ(firstTokenizer.tokenForHash(30u).value(), secondTokenizer.tokenForHash(30u).value());
}

TEST(NesTileVocabularyBuilderTest, DuplicatePatternsCollapse)
{
    NesTileVocabularyBuilder builder;
    builder.addFrame(makeTileFrame({ 20u, 10u, 20u, 10u }));

    NesTileTokenizer tokenizer;
    const auto buildResult = builder.buildFrozenTokenizer(tokenizer);

    ASSERT_TRUE(buildResult.isValue()) << buildResult.errorValue();
    EXPECT_EQ(buildResult.value().sampledTileCount, visibleTileCount());
    EXPECT_EQ(buildResult.value().uniquePatternCount, 2u);
    EXPECT_EQ(tokenizer.getMappedHashCount(), 2u);
}

TEST(NesTileVocabularyBuilderTest, BuildsFromPpuSnapshot)
{
    NesPpuSnapshot snapshot;
    snapshot.mirror = 2u;
    setSolidTilePattern(snapshot, 1u, 1u);
    snapshot.vram[32u] = 1u;

    NesTileVocabularyBuilder builder;
    builder.addSnapshot(snapshot);

    NesTileTokenizer tokenizer;
    const auto buildResult = builder.buildFrozenTokenizer(tokenizer);

    ASSERT_TRUE(buildResult.isValue()) << buildResult.errorValue();
    EXPECT_EQ(buildResult.value().sampledTileCount, visibleTileCount());
    EXPECT_EQ(buildResult.value().uniquePatternCount, 2u);
    EXPECT_EQ(tokenizer.getMode(), NesTileTokenizer::Mode::Frozen);
}

TEST(NesTileVocabularyBuilderTest, OverflowFailsClearly)
{
    NesTileVocabularyBuilder builder;
    builder.addFrame(makeTileFrame({ 10u, 20u }));

    NesTileTokenizer tokenizer(2u);
    const auto buildResult = builder.buildFrozenTokenizer(tokenizer);

    ASSERT_TRUE(buildResult.isError());
    EXPECT_NE(buildResult.errorValue().find("Failed to build vocabulary"), std::string::npos);
    EXPECT_NE(buildResult.errorValue().find("vocabulary exhausted"), std::string::npos);
    EXPECT_EQ(tokenizer.getMode(), NesTileTokenizer::Mode::Learning);
}

TEST(NesTileVocabularyBuilderTest, FrozenTokenizerRejectsUnseenHashes)
{
    NesTileVocabularyBuilder builder;
    builder.addFrame(makeTileFrame({ 10u, 20u }));

    auto tokenizerResult = builder.buildFrozenTokenizer();

    ASSERT_TRUE(tokenizerResult.isValue()) << tokenizerResult.errorValue();
    NesTileTokenizer& tokenizer = tokenizerResult.value();
    EXPECT_EQ(tokenizer.getMode(), NesTileTokenizer::Mode::Frozen);
    const auto unknownResult = tokenizer.tokenForHash(30u);
    ASSERT_TRUE(unknownResult.isError());
    EXPECT_NE(unknownResult.errorValue().find("Frozen vocabulary missing"), std::string::npos);
}

TEST(NesTileVocabularyBuilderTest, RejectsEmptySamples)
{
    NesTileVocabularyBuilder builder;
    NesTileTokenizer tokenizer;

    const auto buildResult = builder.buildFrozenTokenizer(tokenizer);

    ASSERT_TRUE(buildResult.isError());
    EXPECT_NE(buildResult.errorValue().find("without tile samples"), std::string::npos);
}
