#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"
#include "core/scenarios/nes/NesSuperMarioBrosSetupPolicy.h"
#include "core/scenarios/nes/NesSuperMarioBrosTilePosition.h"
#include "core/scenarios/nes/NesTileDebugRenderer.h"
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
constexpr std::array<uint64_t, 5> kProbeFrames = { 300u, 400u, 500u, 700u, 899u };

struct PlayerRelativeProbeSample {
    uint16_t absoluteX = 0;
    uint16_t scrollX = 0;
    uint8_t rawPlayerXScreen = 0;
    int16_t playerVisibleX = 0;
};

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

TEST(NesSuperMarioBrosTileProbeTest, PlayerRelativeXStaysStableAcrossCenteredProbeFrames)
{
    const auto romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "Missing SMB ROM. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "testdata/roms/smb.nes.";
    }

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesSuperMarioBros);
    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romId = "";
    config.romPath = romPath->string();
    config.requireSmolnesMapper = true;

    ASSERT_FALSE(driver.setConfig(ScenarioConfig{ config }).isError());
    ASSERT_FALSE(driver.setup().isError()) << driver.getRuntimeLastError();

    NesSuperMarioBrosRamExtractor extractor;
    Timers timers;
    std::optional<PlayerRelativeProbeSample> frame400Sample;
    std::optional<PlayerRelativeProbeSample> frame500Sample;
    for (uint64_t frameIndex = 0; frameIndex <= 500u; ++frameIndex) {
        const auto stepResult = driver.step(timers, scriptedControllerMaskForFrame(frameIndex));
        if (frameIndex != 400u && frameIndex != 500u) {
            continue;
        }

        const auto ppuSnapshot = driver.copyRuntimePpuSnapshot();
        ASSERT_TRUE(ppuSnapshot.has_value()) << "Missing PPU snapshot at " << frameIndex;
        ASSERT_TRUE(stepResult.memorySnapshot.has_value())
            << "Missing memory snapshot at " << frameIndex;
        const NesTileFrame tileFrame = makeNesTileFrame(ppuSnapshot.value());
        const NesSuperMarioBrosState smbState = extractor.extract(
            stepResult.memorySnapshot.value(), frameIndex >= kSetupScriptEndFrame);
        const PlayerRelativeProbeSample sample{
            .absoluteX = smbState.absoluteX,
            .scrollX = tileFrame.scrollX,
            .rawPlayerXScreen = smbState.playerXScreen,
            .playerVisibleX = makeNesSuperMarioBrosPlayerTileScreenX(smbState, tileFrame.scrollX),
        };
        if (frameIndex == 400u) {
            frame400Sample = sample;
        }
        else {
            frame500Sample = sample;
        }
    }

    ASSERT_TRUE(frame400Sample.has_value());
    ASSERT_TRUE(frame500Sample.has_value());
    EXPECT_LE(
        std::abs(
            static_cast<int>(frame400Sample->playerVisibleX)
            - static_cast<int>(frame500Sample->playerVisibleX)),
        16)
        << "frame 400 visibleX=" << frame400Sample->playerVisibleX
        << " rawX=" << static_cast<int>(frame400Sample->rawPlayerXScreen)
        << " absoluteX=" << frame400Sample->absoluteX << " scrollX=" << frame400Sample->scrollX
        << "; frame 500 visibleX=" << frame500Sample->playerVisibleX
        << " rawX=" << static_cast<int>(frame500Sample->rawPlayerXScreen)
        << " absoluteX=" << frame500Sample->absoluteX << " scrollX=" << frame500Sample->scrollX;
}

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

    NesSuperMarioBrosRamExtractor extractor;
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
        ASSERT_TRUE(stepResult.memorySnapshot.has_value())
            << "Missing memory snapshot at " << frameIndex;
        const NesTileFrame tileFrame = makeNesTileFrame(ppuSnapshot.value());
        const auto tokenFrameResult = makeNesTileTokenFrame(tileFrame, tokenizer);
        ASSERT_TRUE(tokenFrameResult.isValue()) << tokenFrameResult.errorValue();
        const NesSuperMarioBrosState smbState = extractor.extract(
            stepResult.memorySnapshot.value(), frameIndex >= kSetupScriptEndFrame);
        const int16_t playerVisibleX =
            makeNesSuperMarioBrosPlayerTileScreenX(smbState, tileFrame.scrollX);
        const int16_t playerVisibleY = makeNesSuperMarioBrosPlayerTileScreenY(smbState);
        const NesPlayerRelativeTileFrame relativeFrame = makeNesPlayerRelativeTileFrame(
            tokenFrameResult.value(), playerVisibleX, playerVisibleY);
        const auto imageResult = makeNesTileDebugRenderImage(
            NesTileDebugView::Comparison,
            NesTileDebugRenderInput{
                .videoFrame = &stepResult.scenarioVideoFrame.value(),
                .tileFrame = &tileFrame,
                .tokenFrame = &tokenFrameResult.value(),
                .relativeFrame = &relativeFrame,
            });
        ASSERT_TRUE(imageResult.isValue()) << imageResult.errorValue();
        const std::filesystem::path outputPath = comparisonPathForFrame(frameIndex);
        ASSERT_TRUE(writePng(
            imageResult.value().rgba,
            imageResult.value().width,
            imageResult.value().height,
            outputPath))
            << "Failed to write " << outputPath;
        std::cout << "Wrote SMB tile probe: " << outputPath.string() << "\n";
    }
}
