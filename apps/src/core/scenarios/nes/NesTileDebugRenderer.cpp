#include "core/scenarios/nes/NesTileDebugRenderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace DirtSim {
namespace {

constexpr uint32_t kPanelGapPixels = 12u;
constexpr uint32_t kOpaqueBlack = 0x000000FFu;
constexpr uint32_t kPanelBackground = 0x121212FFu;

template <size_t TokenCount>
void drawTokenPanel(
    NesTileDebugRenderImage& image,
    uint32_t panelX,
    uint16_t tileColumns,
    uint16_t tileRows,
    const std::array<NesTileTokenizer::TileToken, TokenCount>& tokens);

Result<NesTileDebugRenderImage, std::string> makeComparisonImage(
    const NesTileDebugRenderInput& input);
Result<NesTileDebugRenderImage, std::string> makeNormalVideoImage(
    const NesTileDebugRenderInput& input);
Result<NesTileDebugRenderImage, std::string> makePatternPixelsImage(
    const NesTileDebugRenderInput& input);
Result<NesTileDebugRenderImage, std::string> makePlayerRelativeTokensImage(
    const NesTileDebugRenderInput& input);
Result<NesTileDebugRenderImage, std::string> makeScreenTokensImage(
    const NesTileDebugRenderInput& input);

uint16_t rgb565FromRgba(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint16_t red5 = static_cast<uint16_t>(red >> 3u);
    const uint16_t green6 = static_cast<uint16_t>(green >> 2u);
    const uint16_t blue5 = static_cast<uint16_t>(blue >> 3u);
    return static_cast<uint16_t>((red5 << 11u) | (green6 << 5u) | blue5);
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

std::array<uint8_t, 4> rgbaFromRgb565(uint16_t value)
{
    const uint8_t red5 = static_cast<uint8_t>((value >> 11u) & 0x1Fu);
    const uint8_t green6 = static_cast<uint8_t>((value >> 5u) & 0x3Fu);
    const uint8_t blue5 = static_cast<uint8_t>(value & 0x1Fu);

    return {
        static_cast<uint8_t>((red5 << 3u) | (red5 >> 2u)),
        static_cast<uint8_t>((green6 << 2u) | (green6 >> 4u)),
        static_cast<uint8_t>((blue5 << 3u) | (blue5 >> 2u)),
        255u,
    };
}

uint16_t readRgb565Pixel(const ScenarioVideoFrame& frame, size_t pixelIndex)
{
    const size_t offset = pixelIndex * 2u;
    const uint8_t lo = std::to_integer<uint8_t>(frame.pixels[offset]);
    const uint8_t hi = std::to_integer<uint8_t>(frame.pixels[offset + 1u]);
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8u));
}

void setRgbaPixel(
    NesTileDebugRenderImage& image,
    uint32_t x,
    uint32_t y,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha)
{
    const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4u;
    image.rgba[offset + 0u] = red;
    image.rgba[offset + 1u] = green;
    image.rgba[offset + 2u] = blue;
    image.rgba[offset + 3u] = alpha;
}

void setRgbaPixel(NesTileDebugRenderImage& image, uint32_t x, uint32_t y, uint32_t color)
{
    const auto rgba = rgbaFromColor(color);
    setRgbaPixel(image, x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
}

NesTileDebugRenderImage makeImage(uint32_t width, uint32_t height, uint32_t fillColor)
{
    NesTileDebugRenderImage image{
        .width = width,
        .height = height,
        .rgba = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4u),
    };

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            setRgbaPixel(image, x, y, fillColor);
        }
    }

    return image;
}

Result<NesTileDebugRenderImage, std::string> errorMissingInput(const std::string& name)
{
    return Result<NesTileDebugRenderImage, std::string>::error(
        "NesTileDebugRenderer: Missing " + name);
}

Result<NesTileDebugRenderImage, std::string> validateVideoFrame(
    const ScenarioVideoFrame& videoFrame)
{
    if (videoFrame.width == 0u || videoFrame.height == 0u) {
        return Result<NesTileDebugRenderImage, std::string>::error(
            "NesTileDebugRenderer: Video frame dimensions must be non-zero");
    }

    const size_t expectedBytes =
        static_cast<size_t>(videoFrame.width) * videoFrame.height * sizeof(uint16_t);
    if (videoFrame.pixels.size() != expectedBytes) {
        return Result<NesTileDebugRenderImage, std::string>::error(
            "NesTileDebugRenderer: Video frame pixel byte size mismatch");
    }

    return Result<NesTileDebugRenderImage, std::string>::okay();
}

Result<NesTileDebugRenderImage, std::string> validateVisibleVideoFrame(
    const ScenarioVideoFrame& videoFrame)
{
    auto validation = validateVideoFrame(videoFrame);
    if (validation.isError()) {
        return validation;
    }

    if (videoFrame.width != NesTileFrame::VisibleWidthPixels
        || videoFrame.height != NesTileFrame::VisibleHeightPixels) {
        return Result<NesTileDebugRenderImage, std::string>::error(
            "NesTileDebugRenderer: NES tile comparison requires a visible 256x224 video frame");
    }

    return Result<NesTileDebugRenderImage, std::string>::okay();
}

void drawVideoPanel(
    NesTileDebugRenderImage& image, const ScenarioVideoFrame& videoFrame, uint32_t panelX)
{
    for (uint32_t y = 0; y < videoFrame.height; ++y) {
        const size_t rowBase = static_cast<size_t>(y) * videoFrame.width;
        for (uint32_t x = 0; x < videoFrame.width; ++x) {
            const auto rgba = rgbaFromRgb565(readRgb565Pixel(videoFrame, rowBase + x));
            setRgbaPixel(image, panelX + x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
        }
    }
}

void drawPatternPanel(
    NesTileDebugRenderImage& image, const NesTileFrame& tileFrame, uint32_t panelX)
{
    for (uint32_t y = 0; y < NesTileFrame::VisibleHeightPixels; ++y) {
        const size_t rowBase = static_cast<size_t>(y) * NesTileFrame::VisibleWidthPixels;
        for (uint32_t x = 0; x < NesTileFrame::VisibleWidthPixels; ++x) {
            const uint8_t shade = static_cast<uint8_t>(tileFrame.patternPixels[rowBase + x] * 85u);
            setRgbaPixel(image, panelX + x, y, shade, shade, shade, 255u);
        }
    }
}

template <size_t TokenCount>
void drawTokenPanel(
    NesTileDebugRenderImage& image,
    uint32_t panelX,
    uint16_t tileColumns,
    uint16_t tileRows,
    const std::array<NesTileTokenizer::TileToken, TokenCount>& tokens)
{
    for (uint32_t gy = 0; gy < tileRows; ++gy) {
        for (uint32_t gx = 0; gx < tileColumns; ++gx) {
            const size_t cellIndex = static_cast<size_t>(gy) * tileColumns + gx;
            const uint32_t color = nesTileDebugTokenColor(tokens[cellIndex]);
            for (uint32_t py = 0; py < NesPlayerRelativeTileFrame::TileSizePixels; ++py) {
                for (uint32_t px = 0; px < NesPlayerRelativeTileFrame::TileSizePixels; ++px) {
                    setRgbaPixel(
                        image,
                        panelX + gx * NesPlayerRelativeTileFrame::TileSizePixels + px,
                        gy * NesPlayerRelativeTileFrame::TileSizePixels + py,
                        color);
                }
            }
        }
    }
}

Result<NesTileDebugRenderImage, std::string> makeComparisonImage(
    const NesTileDebugRenderInput& input)
{
    if (input.videoFrame == nullptr) {
        return errorMissingInput("video frame");
    }
    if (input.tileFrame == nullptr) {
        return errorMissingInput("tile frame");
    }
    if (input.tokenFrame == nullptr) {
        return errorMissingInput("token frame");
    }
    if (input.relativeFrame == nullptr) {
        return errorMissingInput("player-relative tile frame");
    }

    auto validation = validateVisibleVideoFrame(*input.videoFrame);
    if (validation.isError()) {
        return validation;
    }

    const uint32_t panelWidth = NesTileFrame::VisibleWidthPixels;
    const uint32_t panelHeight = NesTileFrame::VisibleHeightPixels;
    const uint32_t relativePanelWidth = NesPlayerRelativeTileFrame::RelativeTileColumns
        * NesPlayerRelativeTileFrame::TileSizePixels;
    const uint32_t relativePanelHeight =
        NesPlayerRelativeTileFrame::RelativeTileRows * NesPlayerRelativeTileFrame::TileSizePixels;
    NesTileDebugRenderImage image = makeImage(
        panelWidth * 3u + relativePanelWidth + kPanelGapPixels * 3u,
        std::max(panelHeight, relativePanelHeight),
        kPanelBackground);

    const uint32_t patternPanelX = panelWidth + kPanelGapPixels;
    const uint32_t tokenPanelX = patternPanelX + panelWidth + kPanelGapPixels;
    const uint32_t relativePanelX = tokenPanelX + panelWidth + kPanelGapPixels;

    drawVideoPanel(image, *input.videoFrame, 0u);
    drawPatternPanel(image, *input.tileFrame, patternPanelX);
    drawTokenPanel(
        image,
        tokenPanelX,
        NesTileTokenFrame::VisibleTileColumns,
        NesTileTokenFrame::VisibleTileRows,
        input.tokenFrame->tokens);
    drawTokenPanel(
        image,
        relativePanelX,
        NesPlayerRelativeTileFrame::RelativeTileColumns,
        NesPlayerRelativeTileFrame::RelativeTileRows,
        input.relativeFrame->tokens);

    return Result<NesTileDebugRenderImage, std::string>::okay(std::move(image));
}

Result<NesTileDebugRenderImage, std::string> makeNormalVideoImage(
    const NesTileDebugRenderInput& input)
{
    if (input.videoFrame == nullptr) {
        return errorMissingInput("video frame");
    }

    auto validation = validateVideoFrame(*input.videoFrame);
    if (validation.isError()) {
        return validation;
    }

    NesTileDebugRenderImage image =
        makeImage(input.videoFrame->width, input.videoFrame->height, kOpaqueBlack);
    drawVideoPanel(image, *input.videoFrame, 0u);
    return Result<NesTileDebugRenderImage, std::string>::okay(std::move(image));
}

Result<NesTileDebugRenderImage, std::string> makePatternPixelsImage(
    const NesTileDebugRenderInput& input)
{
    if (input.tileFrame == nullptr) {
        return errorMissingInput("tile frame");
    }

    NesTileDebugRenderImage image = makeImage(
        NesTileFrame::VisibleWidthPixels, NesTileFrame::VisibleHeightPixels, kOpaqueBlack);
    drawPatternPanel(image, *input.tileFrame, 0u);
    return Result<NesTileDebugRenderImage, std::string>::okay(std::move(image));
}

Result<NesTileDebugRenderImage, std::string> makePlayerRelativeTokensImage(
    const NesTileDebugRenderInput& input)
{
    if (input.relativeFrame == nullptr) {
        return errorMissingInput("player-relative tile frame");
    }

    NesTileDebugRenderImage image = makeImage(
        NesPlayerRelativeTileFrame::RelativeTileColumns
            * NesPlayerRelativeTileFrame::TileSizePixels,
        NesPlayerRelativeTileFrame::RelativeTileRows * NesPlayerRelativeTileFrame::TileSizePixels,
        kOpaqueBlack);
    drawTokenPanel(
        image,
        0u,
        NesPlayerRelativeTileFrame::RelativeTileColumns,
        NesPlayerRelativeTileFrame::RelativeTileRows,
        input.relativeFrame->tokens);
    return Result<NesTileDebugRenderImage, std::string>::okay(std::move(image));
}

Result<NesTileDebugRenderImage, std::string> makeScreenTokensImage(
    const NesTileDebugRenderInput& input)
{
    if (input.tokenFrame == nullptr) {
        return errorMissingInput("token frame");
    }

    NesTileDebugRenderImage image = makeImage(
        NesTileTokenFrame::VisibleTileColumns * NesPlayerRelativeTileFrame::TileSizePixels,
        NesTileTokenFrame::VisibleTileRows * NesPlayerRelativeTileFrame::TileSizePixels,
        kOpaqueBlack);
    drawTokenPanel(
        image,
        0u,
        NesTileTokenFrame::VisibleTileColumns,
        NesTileTokenFrame::VisibleTileRows,
        input.tokenFrame->tokens);
    return Result<NesTileDebugRenderImage, std::string>::okay(std::move(image));
}

} // namespace

Result<NesTileDebugRenderImage, std::string> makeNesTileDebugRenderImage(
    NesTileDebugView view, const NesTileDebugRenderInput& input)
{
    switch (view) {
        case NesTileDebugView::NormalVideo:
            return makeNormalVideoImage(input);
        case NesTileDebugView::PatternPixels:
            return makePatternPixelsImage(input);
        case NesTileDebugView::ScreenTokens:
            return makeScreenTokensImage(input);
        case NesTileDebugView::PlayerRelativeTokens:
            return makePlayerRelativeTokensImage(input);
        case NesTileDebugView::Comparison:
            return makeComparisonImage(input);
    }

    return Result<NesTileDebugRenderImage, std::string>::error(
        "NesTileDebugRenderer: Unknown debug view");
}

ScenarioVideoFrame makeNesTileDebugScenarioVideoFrame(
    const NesTileDebugRenderImage& image, uint64_t frameId)
{
    ScenarioVideoFrame frame{
        .width = static_cast<uint16_t>(image.width),
        .height = static_cast<uint16_t>(image.height),
        .frame_id = frameId,
        .pixels = std::vector<std::byte>(static_cast<size_t>(image.width) * image.height * 2u),
    };

    for (size_t pixelIndex = 0; pixelIndex < static_cast<size_t>(image.width) * image.height;
         ++pixelIndex) {
        const size_t rgbaOffset = pixelIndex * 4u;
        const uint16_t rgb565 = rgb565FromRgba(
            image.rgba[rgbaOffset + 0u], image.rgba[rgbaOffset + 1u], image.rgba[rgbaOffset + 2u]);
        const size_t frameOffset = pixelIndex * 2u;
        frame.pixels[frameOffset + 0u] = static_cast<std::byte>(rgb565 & 0xFFu);
        frame.pixels[frameOffset + 1u] = static_cast<std::byte>((rgb565 >> 8u) & 0xFFu);
    }

    return frame;
}

uint32_t nesTileDebugTokenColor(NesTileTokenizer::TileToken token)
{
    if (token == NesTileTokenizer::VoidToken) {
        return kOpaqueBlack;
    }

    const uint32_t mixed = static_cast<uint32_t>(token) * 2654435761u;
    const uint8_t red = static_cast<uint8_t>(64u + ((mixed >> 0u) & 0x7Fu));
    const uint8_t green = static_cast<uint8_t>(64u + ((mixed >> 8u) & 0x7Fu));
    const uint8_t blue = static_cast<uint8_t>(64u + ((mixed >> 16u) & 0x7Fu));
    return (static_cast<uint32_t>(red) << 24u) | (static_cast<uint32_t>(green) << 16u)
        | (static_cast<uint32_t>(blue) << 8u) | 255u;
}

} // namespace DirtSim
