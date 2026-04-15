#include "core/scenarios/nes/NesTileTokenFrame.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>

using namespace DirtSim;

namespace {

NesTileFrame makeTileFrame(uint64_t frameId, uint16_t scrollX, uint16_t scrollY, uint64_t fillHash)
{
    NesTileFrame frame;
    frame.frameId = frameId;
    frame.scrollX = scrollX;
    frame.scrollY = scrollY;
    frame.tilePatternHashes.fill(fillHash);
    return frame;
}

uint64_t hashForIndex(uint64_t index)
{
    return 0xD6E8FEB86659FD93ull ^ (index * 0x100000001B3ull);
}

} // namespace

TEST(NesTileTokenFrameTest, BuildsTokenFrameAndPreservesMetadata)
{
    NesTileTokenizer tokenizer;
    NesTileFrame tileFrame = makeTileFrame(44u, 12u, 96u, hashForIndex(1u));
    tileFrame.tilePatternHashes[0u] = hashForIndex(2u);
    tileFrame.tilePatternHashes[1u] = hashForIndex(3u);
    tileFrame.tilePatternHashes[2u] = hashForIndex(2u);

    const auto tokenFrameResult = makeNesTileTokenFrame(tileFrame, tokenizer);

    ASSERT_TRUE(tokenFrameResult.isValue()) << tokenFrameResult.errorValue();
    const NesTileTokenFrame& tokenFrame = tokenFrameResult.value();
    EXPECT_EQ(tokenFrame.frameId, 44u);
    EXPECT_EQ(tokenFrame.scrollX, 12u);
    EXPECT_EQ(tokenFrame.scrollY, 96u);
    EXPECT_EQ(tokenFrame.tokens[0u], tokenFrame.tokens[2u]);
    EXPECT_NE(tokenFrame.tokens[0u], tokenFrame.tokens[1u]);
    EXPECT_NE(tokenFrame.tokens[0u], NesTileTokenizer::VoidToken);
}

TEST(NesTileTokenFrameTest, ReappearingHashesKeepStableTokensAcrossFrames)
{
    NesTileTokenizer tokenizer;
    NesTileFrame firstTileFrame = makeTileFrame(1u, 0u, 0u, hashForIndex(10u));
    firstTileFrame.tilePatternHashes[0u] = hashForIndex(11u);
    const auto firstTokenFrameResult = makeNesTileTokenFrame(firstTileFrame, tokenizer);
    ASSERT_TRUE(firstTokenFrameResult.isValue()) << firstTokenFrameResult.errorValue();
    const auto stableToken = firstTokenFrameResult.value().tokens[0u];

    NesTileFrame secondTileFrame = makeTileFrame(2u, 8u, 0u, hashForIndex(12u));
    secondTileFrame.tilePatternHashes[5u] = hashForIndex(11u);
    const auto secondTokenFrameResult = makeNesTileTokenFrame(secondTileFrame, tokenizer);

    ASSERT_TRUE(secondTokenFrameResult.isValue()) << secondTokenFrameResult.errorValue();
    EXPECT_EQ(secondTokenFrameResult.value().tokens[5u], stableToken);
    EXPECT_NE(secondTokenFrameResult.value().tokens[0u], stableToken);
}

TEST(NesTileTokenFrameTest, TokenizerOverflowPropagatesAsError)
{
    NesTileTokenizer tokenizer(3u);
    NesTileFrame tileFrame = makeTileFrame(1u, 0u, 0u, hashForIndex(1u));
    tileFrame.tilePatternHashes[1u] = hashForIndex(2u);
    tileFrame.tilePatternHashes[2u] = hashForIndex(3u);

    const auto tokenFrameResult = makeNesTileTokenFrame(tileFrame, tokenizer);

    ASSERT_TRUE(tokenFrameResult.isError());
    EXPECT_NE(tokenFrameResult.errorValue().find("cell 2"), std::string::npos);
    EXPECT_NE(tokenFrameResult.errorValue().find("vocabulary exhausted"), std::string::npos);
}
