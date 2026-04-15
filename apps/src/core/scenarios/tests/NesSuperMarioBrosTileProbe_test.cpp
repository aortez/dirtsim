#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/NesSuperMarioBrosSetupPolicy.h"
#include "core/scenarios/nes/NesTileFrame.h"
#include "core/scenarios/nes/NesTileTokenFrame.h"
#include "core/scenarios/nes/NesTileTokenizer.h"

#include "external/stb/stb_image_write.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

using namespace DirtSim;

namespace {

constexpr uint64_t kSetupScriptEndFrame = 300u;
constexpr uint32_t kPanelGapPixels = 12u;
constexpr std::array<uint64_t, 5> kProbeFrames = { 300u, 400u, 500u, 700u, 899u };

std::optional<std::filesystem::path> resolveNesSmbFixtureRomPath()
{
    if (const char* romPathEnv = std::getenv("DIRTSIM_NES_SMB_TEST_ROM_PATH");
        romPathEnv != nullptr) {
        const std::filesystem::path romPath{ romPathEnv };
        if (std::filesystem::exists(romPath)) {
            return romPath;
        }
    }

    const std::filesystem::path repoRelativeRomPath =
        std::filesystem::path("testdata") / "roms" / "smb.nes";
    if (std::filesystem::exists(repoRelativeRomPath)) {
        return repoRelativeRomPath;
    }

    return std::nullopt;
}

uint8_t baselineControllerMaskForGameFrame(uint64_t gameFrame)
{
    const bool gradualWalkWindow = gameFrame >= 140u && gameFrame < 220u;
    const bool pressRight = !gradualWalkWindow || ((gameFrame % 2u) == 0u);

    uint8_t mask = pressRight ? SMOLNES_RUNTIME_BUTTON_RIGHT : 0u;
    if (gameFrame % 60u < 15u) {
        mask |= SMOLNES_RUNTIME_BUTTON_A;
    }

    return mask;
}

uint8_t scriptedControllerMaskForFrame(uint64_t frameIndex)
{
    if (frameIndex < kSetupScriptEndFrame) {
        return getNesSuperMarioBrosScriptedSetupMaskForFrame(frameIndex);
    }

    return baselineControllerMaskForGameFrame(frameIndex - kSetupScriptEndFrame);
}

std::array<uint8_t, 4> rgb565ToRgba8888(uint16_t value)
{
    const uint8_t red5 = static_cast<uint8_t>((value >> 11) & 0x1Fu);
    const uint8_t green6 = static_cast<uint8_t>((value >> 5) & 0x3Fu);
    const uint8_t blue5 = static_cast<uint8_t>(value & 0x1Fu);

    return {
        static_cast<uint8_t>((red5 << 3) | (red5 >> 2)),
        static_cast<uint8_t>((green6 << 2) | (green6 >> 4)),
        static_cast<uint8_t>((blue5 << 3) | (blue5 >> 2)),
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
    std::vector<uint8_t>& rgba, uint32_t width, uint32_t x, uint32_t y, uint32_t color)
{
    const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
    rgba[offset + 0u] = static_cast<uint8_t>((color >> 24u) & 0xFFu);
    rgba[offset + 1u] = static_cast<uint8_t>((color >> 16u) & 0xFFu);
    rgba[offset + 2u] = static_cast<uint8_t>((color >> 8u) & 0xFFu);
    rgba[offset + 3u] = static_cast<uint8_t>(color & 0xFFu);
}

uint32_t tileTokenColor(NesTileTokenizer::TileToken token)
{
    if (token == NesTileTokenizer::VoidToken) {
        return 0x000000FFu;
    }

    const uint32_t mixed = static_cast<uint32_t>(token) * 2654435761u;
    const uint8_t red = static_cast<uint8_t>(64u + ((mixed >> 0u) & 0x7Fu));
    const uint8_t green = static_cast<uint8_t>(64u + ((mixed >> 8u) & 0x7Fu));
    const uint8_t blue = static_cast<uint8_t>(64u + ((mixed >> 16u) & 0x7Fu));
    return (static_cast<uint32_t>(red) << 24u) | (static_cast<uint32_t>(green) << 16u)
        | (static_cast<uint32_t>(blue) << 8u) | 255u;
}

std::vector<uint8_t> makeComparisonImage(
    const ScenarioVideoFrame& videoFrame,
    const NesTileFrame& tileFrame,
    const NesTileTokenFrame& tokenFrame)
{
    const uint32_t panelWidth = NesTileFrame::VisibleWidthPixels;
    const uint32_t panelHeight = NesTileFrame::VisibleHeightPixels;
    const uint32_t width = panelWidth * 3u + kPanelGapPixels * 2u;
    const uint32_t height = panelHeight;
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4u, 18u);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            setRgbaPixel(rgba, width, x, y, 0x121212FFu);
        }
    }

    const uint32_t patternPanelX = panelWidth + kPanelGapPixels;
    const uint32_t tokenPanelX = patternPanelX + panelWidth + kPanelGapPixels;

    for (uint32_t y = 0; y < panelHeight; ++y) {
        const size_t rowBase = static_cast<size_t>(y) * panelWidth;
        for (uint32_t x = 0; x < panelWidth; ++x) {
            const size_t pixelIndex = rowBase + x;
            const auto actual = rgb565ToRgba8888(readRgb565Pixel(videoFrame, pixelIndex));
            setRgbaPixel(
                rgba,
                width,
                x,
                y,
                (static_cast<uint32_t>(actual[0]) << 24u)
                    | (static_cast<uint32_t>(actual[1]) << 16u)
                    | (static_cast<uint32_t>(actual[2]) << 8u) | 255u);

            const uint8_t shade = static_cast<uint8_t>(tileFrame.patternPixels[pixelIndex] * 85u);
            setRgbaPixel(
                rgba,
                width,
                patternPanelX + x,
                y,
                (static_cast<uint32_t>(shade) << 24u) | (static_cast<uint32_t>(shade) << 16u)
                    | (static_cast<uint32_t>(shade) << 8u) | 255u);
        }
    }

    for (uint32_t gy = 0; gy < NesTileFrame::VisibleTileRows; ++gy) {
        for (uint32_t gx = 0; gx < NesTileFrame::VisibleTileColumns; ++gx) {
            const size_t cellIndex =
                static_cast<size_t>(gy) * NesTileFrame::VisibleTileColumns + gx;
            const uint32_t color = tileTokenColor(tokenFrame.tokens[cellIndex]);
            for (uint32_t py = 0; py < 8u; ++py) {
                for (uint32_t px = 0; px < 8u; ++px) {
                    setRgbaPixel(rgba, width, tokenPanelX + gx * 8u + px, gy * 8u + py, color);
                }
            }
        }
    }

    return rgba;
}

void pngWriteCallback(void* context, void* data, int size)
{
    auto* buffer = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    buffer->insert(buffer->end(), bytes, bytes + size);
}

bool writePng(
    const std::vector<uint8_t>& rgba,
    uint32_t width,
    uint32_t height,
    const std::filesystem::path& path)
{
    std::vector<uint8_t> png;
    if (stbi_write_png_to_func(
            pngWriteCallback,
            &png,
            static_cast<int>(width),
            static_cast<int>(height),
            4,
            rgba.data(),
            static_cast<int>(width * 4u))
        == 0) {
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    stream.write(
        reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return stream.good();
}

std::filesystem::path comparisonPathForFrame(uint64_t frameIndex)
{
    return std::filesystem::path(::testing::TempDir())
        / ("nes_smb_tile_probe_" + std::to_string(frameIndex) + ".png");
}

} // namespace

TEST(NesSuperMarioBrosTileProbeTest, DISABLED_SavesTileComparisonPngs)
{
    const auto romPath = resolveNesSmbFixtureRomPath();
    ASSERT_TRUE(romPath.has_value())
        << "Missing SMB ROM. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place testdata/roms/smb.nes.";

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesSuperMarioBros);
    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romId = "";
    config.romPath = romPath->string();
    config.requireSmolnesMapper = true;

    ASSERT_FALSE(driver.setConfig(ScenarioConfig{ config }).isError());
    ASSERT_FALSE(driver.setup().isError()) << driver.getRuntimeLastError();

    NesTileTokenizer tokenizer;
    Timers timers;
    const uint64_t finalFrame = kProbeFrames.back();
    for (uint64_t frameIndex = 0; frameIndex <= finalFrame; ++frameIndex) {
        const auto stepResult = driver.step(timers, scriptedControllerMaskForFrame(frameIndex));
        ASSERT_TRUE(stepResult.scenarioVideoFrame.has_value())
            << "Missing video frame at " << frameIndex;

        if (std::find(kProbeFrames.begin(), kProbeFrames.end(), frameIndex) == kProbeFrames.end()) {
            continue;
        }

        const auto ppuSnapshot = driver.copyRuntimePpuSnapshot();
        ASSERT_TRUE(ppuSnapshot.has_value()) << "Missing PPU snapshot at " << frameIndex;
        const NesTileFrame tileFrame = makeNesTileFrame(ppuSnapshot.value());
        const auto tokenFrameResult = makeNesTileTokenFrame(tileFrame, tokenizer);
        ASSERT_TRUE(tokenFrameResult.isValue()) << tokenFrameResult.errorValue();
        const std::vector<uint8_t> rgba = makeComparisonImage(
            stepResult.scenarioVideoFrame.value(), tileFrame, tokenFrameResult.value());
        const std::filesystem::path outputPath = comparisonPathForFrame(frameIndex);
        ASSERT_TRUE(writePng(
            rgba,
            NesTileFrame::VisibleWidthPixels * 3u + kPanelGapPixels * 2u,
            NesTileFrame::VisibleHeightPixels,
            outputPath))
            << "Failed to write " << outputPath;
        std::cout << "Wrote SMB tile probe: " << outputPath.string() << "\n";
    }
}
