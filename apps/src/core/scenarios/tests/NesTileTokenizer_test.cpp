#include "core/scenarios/nes/NesTileTokenizer.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>

using namespace DirtSim;

namespace {

NesTileTokenizer::TilePatternHash hashForIndex(uint64_t index)
{
    return 0x9E3779B97F4A7C15ull ^ (index * 0x100000001B3ull);
}

} // namespace

TEST(NesTileTokenizerTest, VoidMapsToZero)
{
    NesTileTokenizer tokenizer;

    const auto tokenResult = tokenizer.tokenForHash(std::nullopt);

    ASSERT_TRUE(tokenResult.isValue()) << tokenResult.errorValue();
    EXPECT_EQ(tokenResult.value(), NesTileTokenizer::VoidToken);
    EXPECT_EQ(tokenizer.getMappedHashCount(), 0u);
}

TEST(NesTileTokenizerTest, RepeatedHashGetsSameToken)
{
    NesTileTokenizer tokenizer;

    const auto firstTokenResult = tokenizer.tokenForHash(hashForIndex(1u));
    const auto secondTokenResult = tokenizer.tokenForHash(hashForIndex(1u));

    ASSERT_TRUE(firstTokenResult.isValue()) << firstTokenResult.errorValue();
    ASSERT_TRUE(secondTokenResult.isValue()) << secondTokenResult.errorValue();
    EXPECT_EQ(firstTokenResult.value(), secondTokenResult.value());
    EXPECT_NE(firstTokenResult.value(), NesTileTokenizer::VoidToken);
    EXPECT_EQ(tokenizer.getMappedHashCount(), 1u);
}

TEST(NesTileTokenizerTest, DistinctHashesGetDistinctTokens)
{
    NesTileTokenizer tokenizer;

    const auto firstTokenResult = tokenizer.tokenForHash(hashForIndex(1u));
    const auto secondTokenResult = tokenizer.tokenForHash(hashForIndex(2u));
    const auto thirdTokenResult = tokenizer.tokenForHash(hashForIndex(3u));

    ASSERT_TRUE(firstTokenResult.isValue()) << firstTokenResult.errorValue();
    ASSERT_TRUE(secondTokenResult.isValue()) << secondTokenResult.errorValue();
    ASSERT_TRUE(thirdTokenResult.isValue()) << thirdTokenResult.errorValue();
    EXPECT_NE(firstTokenResult.value(), secondTokenResult.value());
    EXPECT_NE(firstTokenResult.value(), thirdTokenResult.value());
    EXPECT_NE(secondTokenResult.value(), thirdTokenResult.value());
}

TEST(NesTileTokenizerTest, ResetClearsMapping)
{
    NesTileTokenizer tokenizer;

    const auto firstTokenResult = tokenizer.tokenForHash(hashForIndex(1u));
    ASSERT_TRUE(firstTokenResult.isValue()) << firstTokenResult.errorValue();
    tokenizer.reset();

    const auto secondTokenResult = tokenizer.tokenForHash(hashForIndex(1u));

    ASSERT_TRUE(secondTokenResult.isValue()) << secondTokenResult.errorValue();
    EXPECT_EQ(firstTokenResult.value(), 1u);
    EXPECT_EQ(secondTokenResult.value(), 1u);
    EXPECT_EQ(tokenizer.getMappedHashCount(), 1u);
}

TEST(NesTileTokenizerTest, SameHashAcrossDifferentFramesStaysStable)
{
    NesTileTokenizer tokenizer;

    const auto frameOneTokenResult = tokenizer.tokenForHash(hashForIndex(10u));
    const auto frameOneNewTokenResult = tokenizer.tokenForHash(hashForIndex(11u));
    const auto frameTwoTokenResult = tokenizer.tokenForHash(hashForIndex(10u));
    const auto frameTwoNewTokenResult = tokenizer.tokenForHash(hashForIndex(12u));
    const auto frameThreeTokenResult = tokenizer.tokenForHash(hashForIndex(10u));

    ASSERT_TRUE(frameOneTokenResult.isValue()) << frameOneTokenResult.errorValue();
    ASSERT_TRUE(frameOneNewTokenResult.isValue()) << frameOneNewTokenResult.errorValue();
    ASSERT_TRUE(frameTwoTokenResult.isValue()) << frameTwoTokenResult.errorValue();
    ASSERT_TRUE(frameTwoNewTokenResult.isValue()) << frameTwoNewTokenResult.errorValue();
    ASSERT_TRUE(frameThreeTokenResult.isValue()) << frameThreeTokenResult.errorValue();
    EXPECT_EQ(frameOneTokenResult.value(), frameTwoTokenResult.value());
    EXPECT_EQ(frameOneTokenResult.value(), frameThreeTokenResult.value());
    EXPECT_NE(frameOneTokenResult.value(), frameOneNewTokenResult.value());
    EXPECT_NE(frameOneTokenResult.value(), frameTwoNewTokenResult.value());
}

TEST(NesTileTokenizerTest, OverflowIsExplicit)
{
    NesTileTokenizer tokenizer(4u);

    EXPECT_TRUE(tokenizer.tokenForHash(hashForIndex(1u)).isValue());
    EXPECT_TRUE(tokenizer.tokenForHash(hashForIndex(2u)).isValue());
    EXPECT_TRUE(tokenizer.tokenForHash(hashForIndex(3u)).isValue());

    const auto overflowResult = tokenizer.tokenForHash(hashForIndex(4u));

    ASSERT_TRUE(overflowResult.isError());
    EXPECT_NE(overflowResult.errorValue().find("vocabulary exhausted"), std::string::npos);
}

TEST(NesTileTokenizerTest, TileIdTokenRemapCollapsesEquivalentPatterns)
{
    NesTileTokenizer tokenizer;
    NesTileTokenizer::TileIdPatternHashTable tilePatternHashes{};
    for (size_t i = 0; i < tilePatternHashes.size(); ++i) {
        tilePatternHashes[i] = hashForIndex(static_cast<uint64_t>(i));
    }
    tilePatternHashes[3u] = hashForIndex(1234u);
    tilePatternHashes[7u] = hashForIndex(1234u);

    const auto remapResult = tokenizer.tileIdTokenRemapBuild(tilePatternHashes);

    ASSERT_TRUE(remapResult.isValue()) << remapResult.errorValue();
    const auto& remap = remapResult.value();
    EXPECT_EQ(remap[3u], remap[7u]);
    EXPECT_NE(remap[3u], NesTileTokenizer::VoidToken);
    EXPECT_NE(remap[2u], remap[3u]);
    EXPECT_EQ(tokenizer.getMappedHashCount(), 255u);
}
