#include "core/scenarios/nes/NesTileDebugRenderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

using namespace DirtSim;

namespace {

ScenarioVideoFrame makeVideoFrame(uint16_t width, uint16_t height, uint16_t fillRgb565 = 0u)
{
    ScenarioVideoFrame frame{
        .width = width,
        .height = height,
        .frame_id = 42u,
        .pixels = std::vector<std::byte>(static_cast<size_t>(width) * height * 2u),
    };

    for (size_t pixelIndex = 0; pixelIndex < static_cast<size_t>(width) * height; ++pixelIndex) {
        const size_t offset = pixelIndex * 2u;
        frame.pixels[offset + 0u] = static_cast<std::byte>(fillRgb565 & 0xFFu);
        frame.pixels[offset + 1u] = static_cast<std::byte>((fillRgb565 >> 8u) & 0xFFu);
    }

    return frame;
}

std::array<uint8_t, 4> rgbaAt(const NesTileDebugRenderImage& image, uint32_t x, uint32_t y)
{
    const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4u;
    return {
        image.rgba[offset + 0u],
        image.rgba[offset + 1u],
        image.rgba[offset + 2u],
        image.rgba[offset + 3u],
    };
}

std::array<uint8_t, 4> rgbaFromColor(uint32_t color)
{
    return {
        static_cast<uint8_t>((color >> 24u) & 0xFFu),
        static_cast<uint8_t>((color >> 16u) & 0xFFu),
        static_cast<uint8_t>((color >> 8u) & 0xFFu),
        static_cast<uint8_t>(color & 0xFFu),
    };
}

void setRgb565Pixel(ScenarioVideoFrame& frame, size_t pixelIndex, uint16_t rgb565)
{
    const size_t offset = pixelIndex * 2u;
    frame.pixels[offset + 0u] = static_cast<std::byte>(rgb565 & 0xFFu);
    frame.pixels[offset + 1u] = static_cast<std::byte>((rgb565 >> 8u) & 0xFFu);
}

} // namespace

TEST(NesTileDebugRendererTest, NormalVideoConvertsRgb565PixelsToRgba)
{
    ScenarioVideoFrame videoFrame = makeVideoFrame(3u, 1u);
    setRgb565Pixel(videoFrame, 0u, 0xF800u);
    setRgb565Pixel(videoFrame, 1u, 0x07E0u);
    setRgb565Pixel(videoFrame, 2u, 0x001Fu);

    const auto imageResult = makeNesTileDebugRenderImage(
        NesTileDebugView::NormalVideo, NesTileDebugRenderInput{ .videoFrame = &videoFrame });

    ASSERT_TRUE(imageResult.isValue()) << imageResult.errorValue();
    const NesTileDebugRenderImage& image = imageResult.value();
    EXPECT_EQ(image.width, 3u);
    EXPECT_EQ(image.height, 1u);
    EXPECT_EQ(rgbaAt(image, 0u, 0u), (std::array<uint8_t, 4>{ 255u, 0u, 0u, 255u }));
    EXPECT_EQ(rgbaAt(image, 1u, 0u), (std::array<uint8_t, 4>{ 0u, 255u, 0u, 255u }));
    EXPECT_EQ(rgbaAt(image, 2u, 0u), (std::array<uint8_t, 4>{ 0u, 0u, 255u, 255u }));
}

TEST(NesTileDebugRendererTest, PatternPixelsRenderAsFourGrayscaleLevels)
{
    NesTileFrame tileFrame;
    tileFrame.patternPixels[0u] = 0u;
    tileFrame.patternPixels[1u] = 1u;
    tileFrame.patternPixels[2u] = 2u;
    tileFrame.patternPixels[3u] = 3u;

    const auto imageResult = makeNesTileDebugRenderImage(
        NesTileDebugView::PatternPixels, NesTileDebugRenderInput{ .tileFrame = &tileFrame });

    ASSERT_TRUE(imageResult.isValue()) << imageResult.errorValue();
    const NesTileDebugRenderImage& image = imageResult.value();
    EXPECT_EQ(image.width, NesTileFrame::VisibleWidthPixels);
    EXPECT_EQ(image.height, NesTileFrame::VisibleHeightPixels);
    EXPECT_EQ(rgbaAt(image, 0u, 0u), (std::array<uint8_t, 4>{ 0u, 0u, 0u, 255u }));
    EXPECT_EQ(rgbaAt(image, 1u, 0u), (std::array<uint8_t, 4>{ 85u, 85u, 85u, 255u }));
    EXPECT_EQ(rgbaAt(image, 2u, 0u), (std::array<uint8_t, 4>{ 170u, 170u, 170u, 255u }));
    EXPECT_EQ(rgbaAt(image, 3u, 0u), (std::array<uint8_t, 4>{ 255u, 255u, 255u, 255u }));
}

TEST(NesTileDebugRendererTest, ScreenTokensRenderDeterministicColorsAndVoidBlack)
{
    NesTileTokenFrame tokenFrame;
    tokenFrame.tokens.fill(NesTileTokenizer::VoidToken);
    tokenFrame.tokens[0u] = 7u;

    const auto imageResult = makeNesTileDebugRenderImage(
        NesTileDebugView::ScreenTokens, NesTileDebugRenderInput{ .tokenFrame = &tokenFrame });

    ASSERT_TRUE(imageResult.isValue()) << imageResult.errorValue();
    const NesTileDebugRenderImage& image = imageResult.value();
    EXPECT_EQ(image.width, NesTileFrame::VisibleWidthPixels);
    EXPECT_EQ(image.height, NesTileFrame::VisibleHeightPixels);
    EXPECT_EQ(rgbaAt(image, 0u, 0u), rgbaFromColor(nesTileDebugTokenColor(7u)));
    EXPECT_EQ(rgbaAt(image, 7u, 7u), rgbaFromColor(nesTileDebugTokenColor(7u)));
    EXPECT_EQ(rgbaAt(image, 8u, 0u), (std::array<uint8_t, 4>{ 0u, 0u, 0u, 255u }));
    EXPECT_EQ(nesTileDebugTokenColor(7u), nesTileDebugTokenColor(7u));
    EXPECT_NE(nesTileDebugTokenColor(7u), nesTileDebugTokenColor(8u));
}

TEST(NesTileDebugRendererTest, PlayerRelativeTokensUseExpandedDimensions)
{
    NesPlayerRelativeTileFrame relativeFrame;
    relativeFrame.tokens.fill(NesTileTokenizer::VoidToken);
    relativeFrame.tokens.back() = 9u;

    const auto imageResult = makeNesTileDebugRenderImage(
        NesTileDebugView::PlayerRelativeTokens,
        NesTileDebugRenderInput{ .relativeFrame = &relativeFrame });

    ASSERT_TRUE(imageResult.isValue()) << imageResult.errorValue();
    const NesTileDebugRenderImage& image = imageResult.value();
    EXPECT_EQ(
        image.width,
        NesPlayerRelativeTileFrame::RelativeTileColumns
            * NesPlayerRelativeTileFrame::TileSizePixels);
    EXPECT_EQ(
        image.height,
        NesPlayerRelativeTileFrame::RelativeTileRows * NesPlayerRelativeTileFrame::TileSizePixels);
    EXPECT_EQ(
        rgbaAt(image, image.width - 1u, image.height - 1u),
        rgbaFromColor(nesTileDebugTokenColor(9u)));
}

TEST(NesTileDebugRendererTest, ComparisonDimensionsIncludeExpandedRelativePanel)
{
    ScenarioVideoFrame videoFrame =
        makeVideoFrame(NesTileFrame::VisibleWidthPixels, NesTileFrame::VisibleHeightPixels);
    NesTileFrame tileFrame;
    NesTileTokenFrame tokenFrame;
    NesPlayerRelativeTileFrame relativeFrame;

    const auto imageResult = makeNesTileDebugRenderImage(
        NesTileDebugView::Comparison,
        NesTileDebugRenderInput{
            .videoFrame = &videoFrame,
            .tileFrame = &tileFrame,
            .tokenFrame = &tokenFrame,
            .relativeFrame = &relativeFrame,
        });

    ASSERT_TRUE(imageResult.isValue()) << imageResult.errorValue();
    const NesTileDebugRenderImage& image = imageResult.value();
    EXPECT_EQ(
        image.width,
        NesTileFrame::VisibleWidthPixels * 3u
            + NesPlayerRelativeTileFrame::RelativeTileColumns
                * NesPlayerRelativeTileFrame::TileSizePixels
            + 36u);
    EXPECT_EQ(
        image.height,
        NesPlayerRelativeTileFrame::RelativeTileRows * NesPlayerRelativeTileFrame::TileSizePixels);
}

TEST(NesTileDebugRendererTest, ScenarioVideoFrameConversionWritesLittleEndianRgb565)
{
    const NesTileDebugRenderImage image{
        .width = 2u,
        .height = 1u,
        .rgba = std::vector<uint8_t>{ 255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u },
    };

    const ScenarioVideoFrame frame = makeNesTileDebugScenarioVideoFrame(image, 77u);

    EXPECT_EQ(frame.width, 2u);
    EXPECT_EQ(frame.height, 1u);
    EXPECT_EQ(frame.frame_id, 77u);
    ASSERT_EQ(frame.pixels.size(), 4u);
    EXPECT_EQ(std::to_integer<uint8_t>(frame.pixels[0u]), 0x00u);
    EXPECT_EQ(std::to_integer<uint8_t>(frame.pixels[1u]), 0xF8u);
    EXPECT_EQ(std::to_integer<uint8_t>(frame.pixels[2u]), 0xE0u);
    EXPECT_EQ(std::to_integer<uint8_t>(frame.pixels[3u]), 0x07u);
}

TEST(NesTileDebugRendererTest, RejectsMissingInputs)
{
    const auto imageResult =
        makeNesTileDebugRenderImage(NesTileDebugView::Comparison, NesTileDebugRenderInput{});

    ASSERT_TRUE(imageResult.isError());
    EXPECT_NE(imageResult.errorValue().find("Missing video frame"), std::string::npos);
}
