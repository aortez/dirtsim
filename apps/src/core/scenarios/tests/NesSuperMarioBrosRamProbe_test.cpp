#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesRamProbe.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"
#include "core/scenarios/nes/NesSuperMarioBrosSetupPolicy.h"
#include "core/scenarios/nes/SmolnesRuntimeBackend.h"
#include "external/stb/stb_image_write.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace DirtSim;

namespace {

constexpr size_t kHorizontalSpeedAddr = 0x0057;
constexpr size_t kPlayerFloatStateAddr = 0x001D;
constexpr size_t kPlayerFacingDirCandidateAAddr = 0x0033;
constexpr size_t kPlayerFacingDirCandidateBAddr = 0x0045;
constexpr size_t kPlayerStateAddr = 0x000E;
constexpr size_t kPlayerXPageAddr = 0x006D;
constexpr size_t kPlayerXScreenAddr = 0x0086;
constexpr size_t kPlayerYScreenAddr = 0x00CE;
constexpr size_t kPlayerXSpeedAbsoluteAddr = 0x0700;
constexpr size_t kVerticalSpeedAddr = 0x009F;
constexpr size_t kP1ButtonsPressedAddr = 0x074A;
constexpr size_t kP2ButtonsPressedAddr = 0x074B;
constexpr size_t kLivesAddr = 0x075A;
constexpr size_t kPowerupStateAddr = 0x0756;
constexpr size_t kWorldAddr = 0x075F;
constexpr size_t kLevelAddr = 0x0760;
constexpr size_t kGameEngineAddr = 0x0770;
constexpr uint64_t kBaselineProbeCaptureFrames = 1100;
constexpr uint64_t kEnemyValidationProbeCaptureFrames = kBaselineProbeCaptureFrames;
constexpr uint64_t kLeftMovementProbeCaptureFrames = 480;
constexpr uint64_t kStandingJumpProbeCaptureFrames = 380;
constexpr uint64_t kSetupScriptEndFrame = 300;
constexpr uint64_t kExpectedFirstEnemyActiveFrame = 395u;
constexpr std::array<uint64_t, 8> kEnemyValidationScreenshotFrames = {
    395u, 420u, 460u, 500u, 680u, 700u, 880u, 899u,
};
constexpr uint64_t kExpectedBaselineLifeDropFrame = 1026;
constexpr uint64_t kLifeLossScreenshotStartFrame = 1012u;
constexpr uint64_t kLifeLossScreenshotEndFrame = 1034u;
constexpr uint64_t kStandingJumpScreenshotStartFrame = kSetupScriptEndFrame + 8u;
constexpr uint64_t kStandingJumpScreenshotEndFrame = kSetupScriptEndFrame + 40u;
constexpr std::array<uint64_t, 14> kBaselineProbeScreenshotFrames = {
    0u, 120u, 240u, 300u, 400u, 500u, 600u, 700u, 800u, 899u, 950u, 1000u, 1050u, 1099u,
};
constexpr std::array<uint64_t, 4> kProbeTargetFrames = { 500u, 600u, 700u, 899u };
constexpr size_t kEnemySlotCount = 5;
constexpr std::array<size_t, kEnemySlotCount> kEnemyActiveAddrs = {
    0x000F, 0x0010, 0x0011, 0x0012, 0x0013
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyTypeAddrs = {
    0x0016, 0x0017, 0x0018, 0x0019, 0x001A
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyXPageAddrs = {
    0x006E, 0x006F, 0x0070, 0x0071, 0x0072
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyXScreenAddrs = {
    0x0087, 0x0088, 0x0089, 0x008A, 0x008B
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyYScreenAddrs = {
    0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3
};

enum class ProbeScriptType : uint8_t {
    Baseline = 0,
    EnemyValidation = 3,
    LeftMovement = 1,
    LifeLoss = 4,
    StandingJump = 2,
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

std::optional<uint16_t> readRgb565Pixel(const ScenarioVideoFrame& frame, size_t pixelIndex)
{
    const size_t offset = pixelIndex * 2u;
    if (offset + 1u >= frame.pixels.size()) {
        return std::nullopt;
    }

    const uint8_t lo = std::to_integer<uint8_t>(frame.pixels[offset]);
    const uint8_t hi = std::to_integer<uint8_t>(frame.pixels[offset + 1u]);
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}

std::array<uint8_t, 3> rgb565ToRgb888(uint16_t value)
{
    const uint8_t red5 = static_cast<uint8_t>((value >> 11) & 0x1Fu);
    const uint8_t green6 = static_cast<uint8_t>((value >> 5) & 0x3Fu);
    const uint8_t blue5 = static_cast<uint8_t>(value & 0x1Fu);

    const uint8_t red8 = static_cast<uint8_t>((red5 << 3) | (red5 >> 2));
    const uint8_t green8 = static_cast<uint8_t>((green6 << 2) | (green6 >> 4));
    const uint8_t blue8 = static_cast<uint8_t>((blue5 << 3) | (blue5 >> 2));

    return { red8, green8, blue8 };
}

bool writeScenarioFramePng(const ScenarioVideoFrame& frame, const std::filesystem::path& path)
{
    if (frame.width == 0 || frame.height == 0) {
        return false;
    }

    const size_t expectedBytes =
        static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 2u;
    if (frame.pixels.size() != expectedBytes) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    std::vector<uint8_t> rgb888(pixelCount * 3u);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const std::optional<uint16_t> rgb565 = readRgb565Pixel(frame, pixelIndex);
        if (!rgb565.has_value()) {
            return false;
        }
        const std::array<uint8_t, 3> rgb = rgb565ToRgb888(rgb565.value());
        const size_t rgbOffset = pixelIndex * 3u;
        rgb888[rgbOffset + 0u] = rgb[0];
        rgb888[rgbOffset + 1u] = rgb[1];
        rgb888[rgbOffset + 2u] = rgb[2];
    }
    return stbi_write_png(
               path.string().c_str(),
               static_cast<int>(frame.width),
               static_cast<int>(frame.height),
               3,
               rgb888.data(),
               static_cast<int>(frame.width * 3u))
        != 0;
}

uint16_t decodeAbsoluteX(const std::vector<uint8_t>& cpuRam)
{
    if (kPlayerXPageAddr >= cpuRam.size() || kPlayerXScreenAddr >= cpuRam.size()) {
        return 0;
    }

    return (static_cast<uint16_t>(cpuRam[kPlayerXPageAddr]) << 8)
        | static_cast<uint16_t>(cpuRam[kPlayerXScreenAddr]);
}

std::string controllerMaskToString(uint8_t controllerMask)
{
    if (controllerMask == 0u) {
        return "None";
    }

    std::string label;
    const auto appendButton = [&label](const char* buttonLabel) {
        if (!label.empty()) {
            label += "+";
        }
        label += buttonLabel;
    };

    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_A) != 0u) {
        appendButton("A");
    }
    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_B) != 0u) {
        appendButton("B");
    }
    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_SELECT) != 0u) {
        appendButton("Select");
    }
    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_START) != 0u) {
        appendButton("Start");
    }
    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_UP) != 0u) {
        appendButton("Up");
    }
    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_DOWN) != 0u) {
        appendButton("Down");
    }
    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_LEFT) != 0u) {
        appendButton("Left");
    }
    if ((controllerMask & SMOLNES_RUNTIME_BUTTON_RIGHT) != 0u) {
        appendButton("Right");
    }

    return label;
}

const char* lifeStateToString(SmbLifeState lifeState)
{
    switch (lifeState) {
        case SmbLifeState::Alive:
            return "Alive";
        case SmbLifeState::Dying:
            return "Dying";
        case SmbLifeState::Dead:
            return "Dead";
    }

    return "Unknown";
}

std::filesystem::path screenshotPathForFrame(ProbeScriptType probeScriptType, uint64_t frameIndex)
{
    switch (probeScriptType) {
        case ProbeScriptType::Baseline:
            return std::filesystem::path(::testing::TempDir())
                / ("nes_smb_frame_" + std::to_string(frameIndex) + ".png");
        case ProbeScriptType::EnemyValidation:
            return std::filesystem::path(::testing::TempDir())
                / ("nes_smb_enemy_frame_" + std::to_string(frameIndex) + ".png");
        case ProbeScriptType::LeftMovement:
            return std::filesystem::path(::testing::TempDir())
                / ("nes_smb_left_frame_" + std::to_string(frameIndex) + ".png");
        case ProbeScriptType::StandingJump:
            return std::filesystem::path(::testing::TempDir())
                / ("nes_smb_standing_jump_frame_" + std::to_string(frameIndex) + ".png");
        case ProbeScriptType::LifeLoss:
            return std::filesystem::path(::testing::TempDir())
                / ("nes_smb_life_loss_frame_" + std::to_string(frameIndex) + ".png");
    }

    return std::filesystem::path(::testing::TempDir())
        / ("nes_smb_frame_" + std::to_string(frameIndex) + ".png");
}

void clearProbeScreenshots(ProbeScriptType probeScriptType)
{
    const std::string prefix = screenshotPathForFrame(probeScriptType, 0u).filename().string();
    const size_t frameMarkerPos = prefix.find("0.png");
    const std::string filenamePrefix =
        frameMarkerPos == std::string::npos ? prefix : prefix.substr(0u, frameMarkerPos);

    const std::filesystem::path tempDir = ::testing::TempDir();
    for (const auto& entry : std::filesystem::directory_iterator(tempDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        if (filename.rfind(filenamePrefix, 0) != 0 || entry.path().extension() != ".png") {
            continue;
        }

        std::error_code ec;
        std::filesystem::remove(entry.path(), ec);
    }
}

uint64_t probeCaptureFrameCount(ProbeScriptType probeScriptType)
{
    switch (probeScriptType) {
        case ProbeScriptType::Baseline:
            return kBaselineProbeCaptureFrames;
        case ProbeScriptType::EnemyValidation:
            return kEnemyValidationProbeCaptureFrames;
        case ProbeScriptType::LeftMovement:
            return kLeftMovementProbeCaptureFrames;
        case ProbeScriptType::StandingJump:
            return kStandingJumpProbeCaptureFrames;
        case ProbeScriptType::LifeLoss:
            return kBaselineProbeCaptureFrames;
    }

    return kBaselineProbeCaptureFrames;
}

bool shouldCaptureScreenshot(ProbeScriptType probeScriptType, uint64_t frameIndex)
{
    switch (probeScriptType) {
        case ProbeScriptType::Baseline:
            return std::find(
                       kBaselineProbeScreenshotFrames.begin(),
                       kBaselineProbeScreenshotFrames.end(),
                       frameIndex)
                != kBaselineProbeScreenshotFrames.end();
        case ProbeScriptType::EnemyValidation:
            return std::find(
                       kEnemyValidationScreenshotFrames.begin(),
                       kEnemyValidationScreenshotFrames.end(),
                       frameIndex)
                != kEnemyValidationScreenshotFrames.end();
        case ProbeScriptType::LeftMovement:
            return false;
        case ProbeScriptType::StandingJump:
            return frameIndex >= kStandingJumpScreenshotStartFrame
                && frameIndex <= kStandingJumpScreenshotEndFrame;
        case ProbeScriptType::LifeLoss:
            return frameIndex >= kLifeLossScreenshotStartFrame
                && frameIndex <= kLifeLossScreenshotEndFrame;
    }

    return false;
}

uint8_t baselineControllerMaskForGameFrame(uint64_t gameFrame)
{
    const bool gradualWalkWindow = gameFrame >= 140u && gameFrame < 220u;
    const bool pressRight = !gradualWalkWindow || ((gameFrame % 2u) == 0u);

    uint8_t mask = pressRight ? SMOLNES_RUNTIME_BUTTON_RIGHT : 0u;
    if (gameFrame % 60 < 15) {
        mask |= SMOLNES_RUNTIME_BUTTON_A;
    }

    return mask;
}

uint8_t scriptedControllerMaskForFrame(uint64_t frameIndex, ProbeScriptType probeScriptType)
{
    if (frameIndex < kSetupScriptEndFrame) {
        return getNesSuperMarioBrosScriptedSetupMaskForFrame(frameIndex);
    }

    const uint64_t gameFrame = frameIndex - kSetupScriptEndFrame;
    switch (probeScriptType) {
        case ProbeScriptType::Baseline:
        case ProbeScriptType::EnemyValidation:
        case ProbeScriptType::LifeLoss:
            return baselineControllerMaskForGameFrame(gameFrame);
        case ProbeScriptType::LeftMovement:
            if (gameFrame < 100u) {
                return SMOLNES_RUNTIME_BUTTON_RIGHT;
            }
            if (gameFrame < 160u) {
                return SMOLNES_RUNTIME_BUTTON_LEFT;
            }
            if (gameFrame < 180u) {
                return SMOLNES_RUNTIME_BUTTON_RIGHT;
            }
            return 0u;
        case ProbeScriptType::StandingJump:
            if (gameFrame < 12u) {
                return 0u;
            }
            if (gameFrame < 18u) {
                return SMOLNES_RUNTIME_BUTTON_A;
            }
            return 0u;
    }

    return 0u;
}

struct CapturedFrame {
    uint64_t frameIndex = 0;
    uint8_t controllerMask = 0;
    std::vector<uint8_t> cpuRam;
};

struct SmbProbeReplaySummary {
    size_t aliveGameplayFrameCount = 0;
    std::optional<uint64_t> firstDoneFrame = std::nullopt;
    std::optional<uint64_t> firstPositiveRewardFrame = std::nullopt;
    size_t gameplayFrameCount = 0;
    uint16_t maxAbsoluteX = 0;
    size_t positiveRewardFrameCount = 0;
    double rewardTotal = 0.0;
};

struct DecodedEnemySlot {
    int16_t dx = 0;
    int16_t dy = 0;
    size_t slot = 0;
    uint32_t distanceSquared = 0;
    uint8_t type = 0;
};

std::vector<CapturedFrame> captureSmbProbeFrames(
    const std::filesystem::path& romPath, bool writeScreenshots, ProbeScriptType probeScriptType)
{
    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesSuperMarioBros);
    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romId = "";
    config.romPath = romPath.string();
    config.requireSmolnesMapper = true;
    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    if (setResult.isError()) {
        ADD_FAILURE() << setResult.errorValue();
        return {};
    }

    const auto setupResult = driver.setup();
    if (setupResult.isError()) {
        ADD_FAILURE() << setupResult.errorValue();
        return {};
    }

    if (!driver.isRuntimeRunning()) {
        ADD_FAILURE() << driver.getRuntimeLastError();
        return {};
    }

    if (!driver.isRuntimeHealthy()) {
        ADD_FAILURE() << driver.getRuntimeLastError();
        return {};
    }

    Timers timers;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;

    if (writeScreenshots) {
        clearProbeScreenshots(probeScriptType);
    }

    const uint64_t captureFrameCount = probeCaptureFrameCount(probeScriptType);

    std::vector<CapturedFrame> frames;
    frames.reserve(static_cast<size_t>(captureFrameCount));
    for (uint64_t frameIndex = 0; frameIndex < captureFrameCount; ++frameIndex) {
        const uint8_t controllerMask = scriptedControllerMaskForFrame(frameIndex, probeScriptType);
        const NesSmolnesScenarioDriver::StepResult stepResult = driver.step(timers, controllerMask);
        scenarioVideoFrame = stepResult.scenarioVideoFrame;

        if (writeScreenshots && shouldCaptureScreenshot(probeScriptType, frameIndex)) {
            if (stepResult.scenarioVideoFrame.has_value()) {
                const std::filesystem::path screenshotPath =
                    screenshotPathForFrame(probeScriptType, frameIndex);
                if (writeScenarioFramePng(stepResult.scenarioVideoFrame.value(), screenshotPath)) {
                    std::cout << "Wrote SMB screenshot: " << screenshotPath.string() << "\n";
                }
            }
        }

        if (!stepResult.memorySnapshot.has_value()) {
            ADD_FAILURE() << "Missing SMB memory snapshot at frame " << frameIndex;
            return {};
        }

        CapturedFrame frame;
        frame.frameIndex = frameIndex;
        frame.controllerMask = controllerMask;
        frame.cpuRam.assign(
            stepResult.memorySnapshot->cpuRam.begin(), stepResult.memorySnapshot->cpuRam.end());
        frames.push_back(frame);
    }

    EXPECT_EQ(driver.getRuntimeRenderedFrameCount(), captureFrameCount);
    return frames;
}

std::optional<size_t> findFirstLifeDropFrameIndex(
    const std::vector<CapturedFrame>& frames, size_t analysisStartIndex)
{
    for (size_t frameIndex = analysisStartIndex + 1u; frameIndex < frames.size(); ++frameIndex) {
        const uint8_t prevLives = frames[frameIndex - 1u].cpuRam[kLivesAddr];
        const uint8_t curLives = frames[frameIndex].cpuRam[kLivesAddr];
        if (curLives < prevLives) {
            return frameIndex;
        }
    }

    return std::nullopt;
}

size_t countLifeDrops(const std::vector<CapturedFrame>& frames, size_t analysisStartIndex)
{
    size_t lifeDropCount = 0;
    for (size_t frameIndex = analysisStartIndex + 1u; frameIndex < frames.size(); ++frameIndex) {
        const uint8_t prevLives = frames[frameIndex - 1u].cpuRam[kLivesAddr];
        const uint8_t curLives = frames[frameIndex].cpuRam[kLivesAddr];
        if (curLives < prevLives) {
            ++lifeDropCount;
        }
    }

    return lifeDropCount;
}

std::vector<DecodedEnemySlot> decodeActiveEnemySlots(const CapturedFrame& frame)
{
    std::vector<DecodedEnemySlot> decoded;
    decoded.reserve(kEnemySlotCount);

    const uint16_t playerAbsoluteX = decodeAbsoluteX(frame.cpuRam);
    const uint8_t playerYScreen = frame.cpuRam[kPlayerYScreenAddr];

    for (size_t slot = 0; slot < kEnemySlotCount; ++slot) {
        if (frame.cpuRam[kEnemyActiveAddrs[slot]] == 0u) {
            continue;
        }
        const uint8_t type = frame.cpuRam[kEnemyTypeAddrs[slot]];
        if (type == 0u) {
            continue;
        }

        const uint16_t enemyAbsoluteX =
            (static_cast<uint16_t>(frame.cpuRam[kEnemyXPageAddrs[slot]]) << 8)
            | static_cast<uint16_t>(frame.cpuRam[kEnemyXScreenAddrs[slot]]);
        const int16_t dx =
            static_cast<int16_t>(enemyAbsoluteX) - static_cast<int16_t>(playerAbsoluteX);
        const int16_t dy = static_cast<int16_t>(frame.cpuRam[kEnemyYScreenAddrs[slot]])
            - static_cast<int16_t>(playerYScreen);
        const int32_t dx32 = static_cast<int32_t>(dx);
        const int32_t dy32 = static_cast<int32_t>(dy);

        decoded.push_back(
            {
                .dx = dx,
                .dy = dy,
                .slot = slot,
                .distanceSquared = static_cast<uint32_t>((dx32 * dx32) + (dy32 * dy32)),
                .type = type,
            });
    }

    std::sort(
        decoded.begin(),
        decoded.end(),
        [](const DecodedEnemySlot& lhs, const DecodedEnemySlot& rhs) {
            return lhs.distanceSquared < rhs.distanceSquared;
        });
    return decoded;
}

SmolnesRuntime::MemorySnapshot makeMemorySnapshot(const CapturedFrame& frame)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    if (frame.cpuRam.size() != snapshot.cpuRam.size()) {
        ADD_FAILURE() << "Unexpected CPU RAM size in captured frame " << frame.frameIndex;
        return snapshot;
    }

    std::copy(frame.cpuRam.begin(), frame.cpuRam.end(), snapshot.cpuRam.begin());
    return snapshot;
}

SmbProbeReplaySummary replaySmbProbeFramesThroughExtractorAndEvaluator(
    const std::vector<CapturedFrame>& frames)
{
    NesSuperMarioBrosRamExtractor extractor;
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    SmbProbeReplaySummary summary;
    uint64_t advancedFrameCount = 0;
    for (const CapturedFrame& frame : frames) {
        advancedFrameCount += 1u;

        const SmolnesRuntime::MemorySnapshot snapshot = makeMemorySnapshot(frame);
        const NesSuperMarioBrosState state =
            extractor.extract(snapshot, advancedFrameCount >= kSetupScriptEndFrame);

        EXPECT_EQ(state.absoluteX, decodeAbsoluteX(frame.cpuRam))
            << "Extractor absoluteX should match the raw x-page/x-screen decode at frame "
            << frame.frameIndex;

        if (state.phase == SmbPhase::Gameplay) {
            summary.gameplayFrameCount++;
            summary.maxAbsoluteX = std::max(summary.maxAbsoluteX, state.absoluteX);
            if (state.lifeState == SmbLifeState::Alive) {
                summary.aliveGameplayFrameCount++;
            }
        }

        const NesSuperMarioBrosEvaluatorOutput output = evaluator.evaluate(
            {
                .advancedFrames = 1,
                .state = state,
            });

        summary.rewardTotal += output.rewardDelta;
        if (output.rewardDelta > 0.0) {
            summary.positiveRewardFrameCount++;
            if (!summary.firstPositiveRewardFrame.has_value()) {
                summary.firstPositiveRewardFrame = frame.frameIndex;
            }
        }

        if (output.done) {
            summary.firstDoneFrame = frame.frameIndex;
            break;
        }
    }

    return summary;
}

} // namespace

TEST(NesSuperMarioBrosRamProbeTest, ManualStep_WritesCandidateRamTraceCsv)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), true, ProbeScriptType::Baseline);
    ASSERT_FALSE(frames.empty());
    ASSERT_EQ(frames.front().cpuRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_CPU_RAM_BYTES));

    const size_t analysisStartIndex = kSetupScriptEndFrame < frames.size()
        ? static_cast<size_t>(kSetupScriptEndFrame)
        : (frames.size() - 1u);

    const std::vector<uint8_t>& gameplayStartRam = frames[analysisStartIndex].cpuRam;
    const uint8_t gameEngineAtStart = gameplayStartRam[kGameEngineAddr];
    std::cout << "Game engine at frame " << kSetupScriptEndFrame << ": "
              << static_cast<uint32_t>(gameEngineAtStart) << "\n";

    bool foundGameplay = false;
    bool playerXChanged = false;
    uint8_t firstGameplayX = 0;
    for (size_t i = analysisStartIndex; i < frames.size(); ++i) {
        const uint8_t engine = frames[i].cpuRam[kGameEngineAddr];
        if (engine == 1) {
            foundGameplay = true;
            const uint8_t lives = frames[i].cpuRam[kLivesAddr];
            EXPECT_GT(lives, 0u) << "Lives should be positive during gameplay at frame " << i;

            const uint8_t world = frames[i].cpuRam[kWorldAddr];
            const uint8_t level = frames[i].cpuRam[kLevelAddr];
            if (i == analysisStartIndex) {
                EXPECT_EQ(world, 0u);
                EXPECT_EQ(level, 0u);
            }

            const uint8_t playerX = frames[i].cpuRam[kPlayerXScreenAddr];
            if (i == analysisStartIndex) {
                firstGameplayX = playerX;
            }
            else if (playerX != firstGameplayX) {
                playerXChanged = true;
            }

            const uint8_t horizontalSpeed = frames[i].cpuRam[kHorizontalSpeedAddr];
            if (playerXChanged && horizontalSpeed > 0) {
                const uint8_t powerup = frames[i].cpuRam[kPowerupStateAddr];
                EXPECT_EQ(powerup, 0u)
                    << "Powerup should be 0 (small) at world 1-1 start, frame " << i;
                break;
            }
        }
    }

    if (foundGameplay) {
        EXPECT_TRUE(playerXChanged) << "Player X should change when pressing RIGHT.";
    }

    const std::filesystem::path signalPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_ram_probe_signals.csv";
    std::ofstream signalStream(signalPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(signalStream.is_open())
        << "Failed to open SMB signal trace: " << signalPath.string();

    signalStream
        << "frame,controller_mask,game_engine,player_state,lives,world,level,player_x_page,"
           "player_x_screen,absolute_x,horizontal_speed,powerup_state,player_y_screen\n";
    for (size_t frameIndex = analysisStartIndex; frameIndex < frames.size(); ++frameIndex) {
        const CapturedFrame& frame = frames[frameIndex];
        signalStream << frame.frameIndex << "," << static_cast<uint32_t>(frame.controllerMask)
                     << "," << static_cast<uint32_t>(frame.cpuRam[kGameEngineAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kPlayerStateAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kLivesAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kWorldAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kLevelAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kPlayerXPageAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kPlayerXScreenAddr]) << ","
                     << decodeAbsoluteX(frame.cpuRam) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kHorizontalSpeedAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kPowerupStateAddr]) << ","
                     << static_cast<uint32_t>(frame.cpuRam[kPlayerYScreenAddr]) << "\n";
    }

    signalStream.close();
    ASSERT_TRUE(signalStream.good());
    EXPECT_TRUE(std::filesystem::exists(signalPath));
    EXPECT_GT(std::filesystem::file_size(signalPath), 0u);
    std::cout << "Wrote SMB RAM probe signals: " << signalPath.string() << "\n";

    const std::filesystem::path targetFramePath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_target_frames.csv";
    std::ofstream targetFrameStream(targetFramePath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(targetFrameStream.is_open())
        << "Failed to open SMB target frame trace: " << targetFramePath.string();
    targetFrameStream
        << "frame,controller_mask,game_engine,player_state,player_x_page,player_x_screen,"
           "absolute_x,player_y_screen,cpu_0x0033,cpu_0x0045,cpu_0x0057,cpu_0x0700";
    for (size_t slot = 0; slot < kEnemySlotCount; ++slot) {
        targetFrameStream << ",enemy" << slot << "_active"
                          << ",enemy" << slot << "_type"
                          << ",enemy" << slot << "_x_page"
                          << ",enemy" << slot << "_x_screen"
                          << ",enemy" << slot << "_y_screen";
    }
    targetFrameStream << "\n";

    for (const uint64_t targetFrame : kProbeTargetFrames) {
        auto it =
            std::find_if(frames.begin(), frames.end(), [targetFrame](const CapturedFrame& frame) {
                return frame.frameIndex == targetFrame;
            });
        ASSERT_TRUE(it != frames.end()) << "Missing probe frame " << targetFrame;

        const CapturedFrame& frame = *it;
        targetFrameStream << frame.frameIndex << "," << static_cast<uint32_t>(frame.controllerMask)
                          << "," << static_cast<uint32_t>(frame.cpuRam[kGameEngineAddr]) << ","
                          << static_cast<uint32_t>(frame.cpuRam[kPlayerStateAddr]) << ","
                          << static_cast<uint32_t>(frame.cpuRam[kPlayerXPageAddr]) << ","
                          << static_cast<uint32_t>(frame.cpuRam[kPlayerXScreenAddr]) << ","
                          << decodeAbsoluteX(frame.cpuRam) << ","
                          << static_cast<uint32_t>(frame.cpuRam[kPlayerYScreenAddr]) << ","
                          << static_cast<uint32_t>(frame.cpuRam[kPlayerFacingDirCandidateAAddr])
                          << ","
                          << static_cast<uint32_t>(frame.cpuRam[kPlayerFacingDirCandidateBAddr])
                          << "," << static_cast<uint32_t>(frame.cpuRam[kHorizontalSpeedAddr]) << ","
                          << static_cast<uint32_t>(frame.cpuRam[kPlayerXSpeedAbsoluteAddr]);
        for (size_t slot = 0; slot < kEnemySlotCount; ++slot) {
            targetFrameStream << "," << static_cast<uint32_t>(frame.cpuRam[kEnemyActiveAddrs[slot]])
                              << "," << static_cast<uint32_t>(frame.cpuRam[kEnemyTypeAddrs[slot]])
                              << "," << static_cast<uint32_t>(frame.cpuRam[kEnemyXPageAddrs[slot]])
                              << ","
                              << static_cast<uint32_t>(frame.cpuRam[kEnemyXScreenAddrs[slot]])
                              << ","
                              << static_cast<uint32_t>(frame.cpuRam[kEnemyYScreenAddrs[slot]]);
        }
        targetFrameStream << "\n";
    }

    targetFrameStream.close();
    ASSERT_TRUE(targetFrameStream.good());
    EXPECT_TRUE(std::filesystem::exists(targetFramePath));
    EXPECT_GT(std::filesystem::file_size(targetFramePath), 0u);
    std::cout << "Wrote SMB target frame trace: " << targetFramePath.string() << "\n";

    const size_t lifeDropCount = countLifeDrops(frames, analysisStartIndex);
    const std::optional<size_t> firstLifeDropFrameIndex =
        findFirstLifeDropFrameIndex(frames, analysisStartIndex);

    EXPECT_EQ(lifeDropCount, 1u) << "Expected the scripted run to lose exactly one life.";
    ASSERT_TRUE(firstLifeDropFrameIndex.has_value()) << "Expected the scripted run to lose a life.";

    size_t playerXPageAdvanceCount = 0;
    int16_t largestBackwardStep = 0;
    uint16_t bestAbsoluteX = decodeAbsoluteX(frames[analysisStartIndex].cpuRam);
    uint16_t lastAbsoluteX = bestAbsoluteX;
    for (size_t frameIndex = analysisStartIndex + 1u; frameIndex < firstLifeDropFrameIndex.value();
         ++frameIndex) {
        const std::vector<uint8_t>& previous = frames[frameIndex - 1u].cpuRam;
        const std::vector<uint8_t>& current = frames[frameIndex].cpuRam;
        const uint16_t absoluteX = decodeAbsoluteX(current);
        const int16_t absoluteXDelta =
            static_cast<int16_t>(absoluteX) - static_cast<int16_t>(lastAbsoluteX);
        if (current[kPlayerXPageAddr] > previous[kPlayerXPageAddr]) {
            ++playerXPageAdvanceCount;
            EXPECT_GT(absoluteX, lastAbsoluteX)
                << "absoluteX should continue forward when x-page advances at frame "
                << frames[frameIndex].frameIndex;
            EXPECT_LE(absoluteXDelta, 2)
                << "absoluteX should stay continuous across an x-page rollover at frame "
                << frames[frameIndex].frameIndex;
        }

        largestBackwardStep = std::min(largestBackwardStep, absoluteXDelta);
        bestAbsoluteX = std::max(bestAbsoluteX, absoluteX);
        lastAbsoluteX = absoluteX;
    }

    EXPECT_GE(largestBackwardStep, -1)
        << "Expected the scripted run to exhibit at most a one-pixel backward step.";
    EXPECT_GE(bestAbsoluteX, 512u)
        << "Expected absoluteX to cross at least two x-page boundaries before the scripted death.";
    EXPECT_GE(playerXPageAdvanceCount, 2u)
        << "Expected x-page to advance across at least two page boundaries before the scripted "
           "death.";

    struct AddrStats {
        uint32_t changes = 0;
        uint32_t nonzeroFrames = 0;
        uint8_t minValue = 255;
        uint8_t maxValue = 0;
        uint64_t absDiffSum = 0;
        uint32_t smallDiffChanges = 0;
    };

    std::vector<AddrStats> stats(frames.front().cpuRam.size());
    for (size_t frameIndex = analysisStartIndex; frameIndex < frames.size(); ++frameIndex) {
        const std::vector<uint8_t>& current = frames[frameIndex].cpuRam;
        for (size_t addr = 0; addr < current.size(); ++addr) {
            AddrStats& st = stats[addr];
            const uint8_t value = current[addr];
            if (value != 0) {
                st.nonzeroFrames++;
            }
            st.minValue = std::min(st.minValue, value);
            st.maxValue = std::max(st.maxValue, value);
        }
    }

    std::vector<uint32_t> changeCounts(frames.front().cpuRam.size(), 0);
    for (size_t frameIndex = analysisStartIndex + 1u; frameIndex < frames.size(); ++frameIndex) {
        const std::vector<uint8_t>& previous = frames[frameIndex - 1].cpuRam;
        const std::vector<uint8_t>& current = frames[frameIndex].cpuRam;
        ASSERT_EQ(previous.size(), current.size());
        for (size_t addr = 0; addr < current.size(); ++addr) {
            const uint8_t cur = current[addr];
            const uint8_t prev = previous[addr];
            if (cur != prev) {
                changeCounts[addr]++;

                AddrStats& st = stats[addr];
                st.changes++;

                const uint8_t diff = cur > prev ? static_cast<uint8_t>(cur - prev)
                                                : static_cast<uint8_t>(prev - cur);
                st.absDiffSum += diff;
                if (diff <= 10) {
                    st.smallDiffChanges++;
                }
            }
        }
    }

    const std::filesystem::path statsPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_ram_probe_stats.csv";
    std::ofstream statsStream(statsPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(statsStream.is_open()) << "Failed to open probe stats: " << statsPath.string();

    statsStream << "addr,changes,nonzero_frames,min,max,range,abs_diff_sum,small_diff_changes\n";
    for (size_t addr = 0; addr < stats.size(); ++addr) {
        const AddrStats& st = stats[addr];
        const uint32_t range =
            static_cast<uint32_t>(st.maxValue) - static_cast<uint32_t>(st.minValue);
        statsStream << addr << "," << st.changes << "," << st.nonzeroFrames << ","
                    << static_cast<uint32_t>(st.minValue) << ","
                    << static_cast<uint32_t>(st.maxValue) << "," << range << "," << st.absDiffSum
                    << "," << st.smallDiffChanges << "\n";
    }

    statsStream.close();
    ASSERT_TRUE(statsStream.good());
    EXPECT_TRUE(std::filesystem::exists(statsPath));
    EXPECT_GT(std::filesystem::file_size(statsPath), 0u);
    std::cout << "Wrote SMB RAM probe stats: " << statsPath.string() << "\n";
    std::cout << "First life loss frame: " << frames[firstLifeDropFrameIndex.value()].frameIndex
              << ", best absoluteX before death: " << bestAbsoluteX
              << ", largest backward step: " << largestBackwardStep << "\n";

    std::vector<size_t> rankedAddresses(changeCounts.size(), 0);
    std::iota(rankedAddresses.begin(), rankedAddresses.end(), 0);
    std::sort(
        rankedAddresses.begin(), rankedAddresses.end(), [&changeCounts](size_t lhs, size_t rhs) {
            return changeCounts[lhs] > changeCounts[rhs];
        });

    const size_t candidateCount = std::min<size_t>(64, rankedAddresses.size());
    rankedAddresses.resize(candidateCount);

    const std::filesystem::path tracePath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_ram_probe_candidates.csv";
    std::ofstream stream(tracePath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(stream.is_open()) << "Failed to open probe trace: " << tracePath.string();

    stream << "frame,controller_mask";
    for (size_t addr : rankedAddresses) {
        stream << ",cpu_" << addr;
    }
    stream << "\n";

    for (const CapturedFrame& frame : frames) {
        if (frame.frameIndex < kSetupScriptEndFrame) {
            continue;
        }

        stream << frame.frameIndex << "," << static_cast<uint32_t>(frame.controllerMask);
        for (size_t addr : rankedAddresses) {
            if (addr < frame.cpuRam.size()) {
                stream << "," << static_cast<uint32_t>(frame.cpuRam[addr]);
            }
            else {
                stream << ",0";
            }
        }
        stream << "\n";
    }

    stream.close();
    ASSERT_TRUE(stream.good());
    EXPECT_TRUE(std::filesystem::exists(tracePath));
    EXPECT_GT(std::filesystem::file_size(tracePath), 0u);

    std::cout << "Wrote SMB RAM probe trace: " << tracePath.string() << "\n";
    std::cout << "Top changed address: cpu_" << rankedAddresses.front() << " (changed "
              << changeCounts[rankedAddresses.front()] << " times)\n";
}

TEST(NesSuperMarioBrosRamProbeTest, ManualStep_WritesLeftMovementDirectionCsv)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), false, ProbeScriptType::LeftMovement);
    ASSERT_FALSE(frames.empty());
    ASSERT_EQ(frames.front().cpuRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_CPU_RAM_BYTES));

    const size_t analysisStartIndex = kSetupScriptEndFrame < frames.size()
        ? static_cast<size_t>(kSetupScriptEndFrame)
        : (frames.size() - 1u);

    bool foundNegativeHorizontalSpeedWhileHoldingLeft = false;
    bool foundPlayerXDecreaseWhileHoldingLeft = false;
    bool foundFacingLeftWhileStillMovingRight = false;
    bool foundFacingRightWhileStillMovingLeft = false;
    std::set<uint8_t> candidateAValuesWhileHoldingLeft;
    std::set<uint8_t> candidateAValuesWhileHoldingRight;
    std::set<uint8_t> candidateBValuesWhileHoldingLeft;
    std::set<uint8_t> candidateBValuesWhileHoldingRight;

    const std::filesystem::path leftProbePath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_left_probe.csv";
    std::ofstream leftProbeStream(leftProbePath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(leftProbeStream.is_open())
        << "Failed to open SMB left-direction probe trace: " << leftProbePath.string();
    leftProbeStream
        << "frame,controller_mask,game_engine,player_state,player_x_page,player_x_screen,"
           "absolute_x,cpu_0x0033,cpu_0x0045,cpu_0x0057_raw,cpu_0x0057_signed,cpu_0x0700\n";

    for (size_t frameIndex = analysisStartIndex; frameIndex < frames.size(); ++frameIndex) {
        const CapturedFrame& frame = frames[frameIndex];
        const int8_t horizontalSpeedSigned =
            static_cast<int8_t>(frame.cpuRam[kHorizontalSpeedAddr]);

        leftProbeStream << frame.frameIndex << "," << static_cast<uint32_t>(frame.controllerMask)
                        << "," << static_cast<uint32_t>(frame.cpuRam[kGameEngineAddr]) << ","
                        << static_cast<uint32_t>(frame.cpuRam[kPlayerStateAddr]) << ","
                        << static_cast<uint32_t>(frame.cpuRam[kPlayerXPageAddr]) << ","
                        << static_cast<uint32_t>(frame.cpuRam[kPlayerXScreenAddr]) << ","
                        << decodeAbsoluteX(frame.cpuRam) << ","
                        << static_cast<uint32_t>(frame.cpuRam[kPlayerFacingDirCandidateAAddr])
                        << ","
                        << static_cast<uint32_t>(frame.cpuRam[kPlayerFacingDirCandidateBAddr])
                        << "," << static_cast<uint32_t>(frame.cpuRam[kHorizontalSpeedAddr]) << ","
                        << static_cast<int32_t>(horizontalSpeedSigned) << ","
                        << static_cast<uint32_t>(frame.cpuRam[kPlayerXSpeedAbsoluteAddr]) << "\n";

        const bool holdingLeft = frame.controllerMask == SMOLNES_RUNTIME_BUTTON_LEFT;
        const bool holdingRight = frame.controllerMask == SMOLNES_RUNTIME_BUTTON_RIGHT;
        if (holdingLeft) {
            candidateAValuesWhileHoldingLeft.insert(frame.cpuRam[kPlayerFacingDirCandidateAAddr]);
            candidateBValuesWhileHoldingLeft.insert(frame.cpuRam[kPlayerFacingDirCandidateBAddr]);
            if (horizontalSpeedSigned < 0) {
                foundNegativeHorizontalSpeedWhileHoldingLeft = true;
            }
            if (horizontalSpeedSigned > 0 && frame.cpuRam[kPlayerFacingDirCandidateAAddr] == 2u
                && frame.cpuRam[kPlayerFacingDirCandidateBAddr] == 1u) {
                foundFacingLeftWhileStillMovingRight = true;
            }
        }
        if (holdingRight) {
            candidateAValuesWhileHoldingRight.insert(frame.cpuRam[kPlayerFacingDirCandidateAAddr]);
            candidateBValuesWhileHoldingRight.insert(frame.cpuRam[kPlayerFacingDirCandidateBAddr]);
            if (horizontalSpeedSigned < 0 && frame.cpuRam[kPlayerFacingDirCandidateAAddr] == 1u
                && frame.cpuRam[kPlayerFacingDirCandidateBAddr] == 2u) {
                foundFacingRightWhileStillMovingLeft = true;
            }
        }

        if (frameIndex == analysisStartIndex || !holdingLeft) {
            continue;
        }

        const CapturedFrame& previousFrame = frames[frameIndex - 1u];
        if (frame.cpuRam[kPlayerXScreenAddr] < previousFrame.cpuRam[kPlayerXScreenAddr]
            || decodeAbsoluteX(frame.cpuRam) < decodeAbsoluteX(previousFrame.cpuRam)) {
            foundPlayerXDecreaseWhileHoldingLeft = true;
        }
    }

    leftProbeStream.close();
    ASSERT_TRUE(leftProbeStream.good());
    EXPECT_TRUE(std::filesystem::exists(leftProbePath));
    EXPECT_GT(std::filesystem::file_size(leftProbePath), 0u);
    std::cout << "Wrote SMB left probe trace: " << leftProbePath.string() << "\n";

    EXPECT_TRUE(foundNegativeHorizontalSpeedWhileHoldingLeft)
        << "Expected signed horizontal speed to go negative while holding LEFT.";
    EXPECT_TRUE(foundPlayerXDecreaseWhileHoldingLeft)
        << "Expected player X to decrease during the scripted LEFT window.";
    EXPECT_TRUE(foundFacingLeftWhileStillMovingRight)
        << "Expected 0x0033 to flip left before 0x0045 when the player turns around.";
    EXPECT_TRUE(foundFacingRightWhileStillMovingLeft)
        << "Expected 0x0033 to flip right before 0x0045 when the player turns back around.";

    std::cout << "Left probe candidate 0x0033 right/left values:";
    for (const uint8_t value : candidateAValuesWhileHoldingRight) {
        std::cout << " R" << static_cast<uint32_t>(value);
    }
    for (const uint8_t value : candidateAValuesWhileHoldingLeft) {
        std::cout << " L" << static_cast<uint32_t>(value);
    }
    std::cout << "\n";

    std::cout << "Left probe candidate 0x0045 right/left values:";
    for (const uint8_t value : candidateBValuesWhileHoldingRight) {
        std::cout << " R" << static_cast<uint32_t>(value);
    }
    for (const uint8_t value : candidateBValuesWhileHoldingLeft) {
        std::cout << " L" << static_cast<uint32_t>(value);
    }
    std::cout << "\n";
}

TEST(NesSuperMarioBrosRamProbeTest, ManualStep_WritesStandingJumpValidationArtifacts)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), true, ProbeScriptType::StandingJump);
    ASSERT_FALSE(frames.empty());
    ASSERT_EQ(frames.front().cpuRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_CPU_RAM_BYTES));

    const size_t analysisStartIndex = kSetupScriptEndFrame < frames.size()
        ? static_cast<size_t>(kSetupScriptEndFrame)
        : (frames.size() - 1u);

    const uint8_t baselinePlayerY = frames[analysisStartIndex].cpuRam[kPlayerYScreenAddr];

    const std::filesystem::path csvPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_standing_jump_validation.csv";
    std::ofstream csvStream(csvPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(csvStream.is_open())
        << "Failed to open standing jump validation CSV: " << csvPath.string();

    csvStream
        << "frame,game_frame,phase_label,controller_mask,controller_label,cpu_0x000E_player_state,"
           "cpu_0x001D_float_state,cpu_0x009F_vertical_speed_raw,cpu_0x009F_vertical_speed_signed,"
           "cpu_0x00CE_player_y,cpu_0x074A_buttons,raw_0x000E_candidate_airborne,"
           "raw_0x001D_candidate_airborne,extractor_airborne,extractor_life_state,"
           "extractor_vertical_speed_normalized,screenshot\n";

    NesSuperMarioBrosRamExtractor extractor;
    size_t screenshotCount = 0u;
    bool sawButtonMaskCopy = false;
    bool sawPlayerYRise = false;
    bool sawRawFloatStateChange = false;
    bool sawRawPlayerStateChange = false;
    std::set<uint8_t> observedFloatStates;
    std::set<uint8_t> observedPlayerStates;

    const auto phaseLabelForFrame = [](uint64_t gameFrame) -> const char* {
        if (gameFrame < 12u) {
            return "Settle";
        }
        if (gameFrame < 18u) {
            return "HoldJump";
        }
        return "Recovery";
    };

    for (size_t frameIndex = analysisStartIndex; frameIndex < frames.size(); ++frameIndex) {
        const CapturedFrame& frame = frames[frameIndex];
        const uint64_t gameFrame = frame.frameIndex - kSetupScriptEndFrame;
        const SmolnesRuntime::MemorySnapshot snapshot = makeMemorySnapshot(frame);
        const NesSuperMarioBrosState state = extractor.extract(snapshot, true);
        const uint8_t rawPlayerState = frame.cpuRam[kPlayerStateAddr];
        const uint8_t rawFloatState = frame.cpuRam[kPlayerFloatStateAddr];
        const int8_t signedVerticalSpeed = static_cast<int8_t>(frame.cpuRam[kVerticalSpeedAddr]);
        const bool rawPlayerStateCandidateAirborne = rawPlayerState >= 1u && rawPlayerState <= 3u;
        const bool rawFloatStateCandidateAirborne =
            rawFloatState == 0x01u || rawFloatState == 0x02u;
        const bool hasScreenshot =
            shouldCaptureScreenshot(ProbeScriptType::StandingJump, frame.frameIndex);
        const std::filesystem::path screenshotPath = hasScreenshot
            ? screenshotPathForFrame(ProbeScriptType::StandingJump, frame.frameIndex)
            : std::filesystem::path{};

        observedFloatStates.insert(rawFloatState);
        observedPlayerStates.insert(rawPlayerState);
        sawButtonMaskCopy |= frame.cpuRam[kP1ButtonsPressedAddr] == 0x80u;
        sawPlayerYRise |= frame.cpuRam[kPlayerYScreenAddr] < baselinePlayerY;
        sawRawFloatStateChange |=
            rawFloatState != frames[analysisStartIndex].cpuRam[kPlayerFloatStateAddr];
        sawRawPlayerStateChange |=
            rawPlayerState != frames[analysisStartIndex].cpuRam[kPlayerStateAddr];
        if (hasScreenshot && std::filesystem::exists(screenshotPath)) {
            ++screenshotCount;
        }

        csvStream << frame.frameIndex << "," << gameFrame << "," << phaseLabelForFrame(gameFrame)
                  << "," << static_cast<uint32_t>(frame.controllerMask) << ","
                  << controllerMaskToString(frame.controllerMask) << ","
                  << static_cast<uint32_t>(rawPlayerState) << ","
                  << static_cast<uint32_t>(rawFloatState) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kVerticalSpeedAddr]) << ","
                  << static_cast<int32_t>(signedVerticalSpeed) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kPlayerYScreenAddr]) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kP1ButtonsPressedAddr]) << ","
                  << (rawPlayerStateCandidateAirborne ? 1 : 0) << ","
                  << (rawFloatStateCandidateAirborne ? 1 : 0) << "," << (state.airborne ? 1 : 0)
                  << "," << lifeStateToString(state.lifeState) << ","
                  << state.verticalSpeedNormalized << ","
                  << (hasScreenshot ? screenshotPath.filename().string() : "") << "\n";
    }

    csvStream.close();
    ASSERT_TRUE(csvStream.good());
    EXPECT_TRUE(std::filesystem::exists(csvPath));
    EXPECT_GT(std::filesystem::file_size(csvPath), 0u);

    const std::filesystem::path notesPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_standing_jump_validation_notes.txt";
    std::ofstream notesStream(notesPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(notesStream.is_open())
        << "Failed to open standing jump validation notes: " << notesPath.string();
    notesStream << "Standing jump RAM validation artifacts.\n"
                << "Frames " << kStandingJumpScreenshotStartFrame << "-"
                << kStandingJumpScreenshotEndFrame << " include screenshots.\n"
                << "Compare the first visually airborne frame against cpu_0x000E_player_state,\n"
                << "cpu_0x001D_float_state, and extractor_airborne in the CSV.\n"
                << "Observed raw 0x000E values:";
    for (const uint8_t value : observedPlayerStates) {
        notesStream << " " << static_cast<uint32_t>(value);
    }
    notesStream << "\nObserved raw 0x001D values:";
    for (const uint8_t value : observedFloatStates) {
        notesStream << " " << static_cast<uint32_t>(value);
    }
    notesStream << "\n";
    notesStream.close();
    ASSERT_TRUE(notesStream.good());

    const size_t expectedScreenshotCount = static_cast<size_t>(
        kStandingJumpScreenshotEndFrame - kStandingJumpScreenshotStartFrame + 1u);
    EXPECT_EQ(screenshotCount, expectedScreenshotCount)
        << "Expected a contiguous screenshot sequence for the standing jump validation window.";
    EXPECT_TRUE(sawButtonMaskCopy) << "Expected SMB button mask 0x074A to reflect the A press.";
    EXPECT_TRUE(sawPlayerYRise) << "Expected the scripted standing jump to visibly raise Mario.";
    EXPECT_TRUE(sawRawFloatStateChange || sawRawPlayerStateChange)
        << "Expected at least one jump-related raw state register to change during the probe.";

    std::cout << "Wrote SMB standing jump validation CSV: " << csvPath.string() << "\n";
    std::cout << "Wrote SMB standing jump validation notes: " << notesPath.string() << "\n";
}

TEST(NesSuperMarioBrosRamProbeTest, ManualStep_WritesLifeLossValidationArtifacts)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), true, ProbeScriptType::LifeLoss);
    ASSERT_FALSE(frames.empty());

    const size_t analysisStartIndex = kSetupScriptEndFrame < frames.size()
        ? static_cast<size_t>(kSetupScriptEndFrame)
        : (frames.size() - 1u);
    const std::optional<size_t> firstLifeDropFrameIndex =
        findFirstLifeDropFrameIndex(frames, analysisStartIndex);

    ASSERT_TRUE(firstLifeDropFrameIndex.has_value()) << "Expected scripted run to lose a life.";
    ASSERT_EQ(frames[firstLifeDropFrameIndex.value()].frameIndex, kExpectedBaselineLifeDropFrame);
    EXPECT_EQ(countLifeDrops(frames, analysisStartIndex), 1u)
        << "Expected the scripted run to lose exactly one life.";

    const std::filesystem::path csvPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_life_loss_validation.csv";
    std::ofstream csvStream(csvPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(csvStream.is_open())
        << "Failed to open life-loss validation CSV: " << csvPath.string();

    csvStream << "frame,game_frame,controller_mask,controller_label,game_engine,lives,"
                 "cpu_0x000E_player_state,cpu_0x001D_float_state,cpu_0x00CE_player_y,absolute_x,"
                 "extractor_phase,extractor_life_state,raw_is_death_animation,raw_is_player_dies,"
                 "raw_is_reload,screenshot\n";

    NesSuperMarioBrosRamExtractor extractor;
    bool sawDeathAnimationBeforeLifeDrop = false;
    bool sawPlayerDiesFrame = false;
    bool sawReloadFrameAfterLifeDrop = false;
    size_t screenshotCount = 0u;

    for (const CapturedFrame& frame : frames) {
        if (frame.frameIndex < kLifeLossScreenshotStartFrame
            || frame.frameIndex > kLifeLossScreenshotEndFrame) {
            continue;
        }

        const SmolnesRuntime::MemorySnapshot snapshot = makeMemorySnapshot(frame);
        const NesSuperMarioBrosState state = extractor.extract(snapshot, true);
        const uint8_t rawPlayerState = frame.cpuRam[kPlayerStateAddr];
        const bool rawIsDeathAnimation = rawPlayerState == 0x0Bu;
        const bool rawIsPlayerDies = rawPlayerState == 0x06u;
        const bool rawIsReload = rawPlayerState == 0x00u;
        const bool hasScreenshot =
            shouldCaptureScreenshot(ProbeScriptType::LifeLoss, frame.frameIndex);
        const std::filesystem::path screenshotPath = hasScreenshot
            ? screenshotPathForFrame(ProbeScriptType::LifeLoss, frame.frameIndex)
            : std::filesystem::path{};

        if (frame.frameIndex < frames[firstLifeDropFrameIndex.value()].frameIndex) {
            sawDeathAnimationBeforeLifeDrop |= rawIsDeathAnimation;
            sawPlayerDiesFrame |= rawIsPlayerDies;
        }
        else {
            sawReloadFrameAfterLifeDrop |= rawIsReload;
        }
        if (hasScreenshot && std::filesystem::exists(screenshotPath)) {
            ++screenshotCount;
        }

        csvStream << frame.frameIndex << "," << (frame.frameIndex - kSetupScriptEndFrame) << ","
                  << static_cast<uint32_t>(frame.controllerMask) << ","
                  << controllerMaskToString(frame.controllerMask) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kGameEngineAddr]) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kLivesAddr]) << ","
                  << static_cast<uint32_t>(rawPlayerState) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kPlayerFloatStateAddr]) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kPlayerYScreenAddr]) << ","
                  << decodeAbsoluteX(frame.cpuRam) << ","
                  << (state.phase == SmbPhase::Gameplay ? "Gameplay" : "NonGameplay") << ","
                  << lifeStateToString(state.lifeState) << "," << (rawIsDeathAnimation ? 1 : 0)
                  << "," << (rawIsPlayerDies ? 1 : 0) << "," << (rawIsReload ? 1 : 0) << ","
                  << (hasScreenshot ? screenshotPath.filename().string() : "") << "\n";
    }

    csvStream.close();
    ASSERT_TRUE(csvStream.good());
    EXPECT_TRUE(std::filesystem::exists(csvPath));
    EXPECT_GT(std::filesystem::file_size(csvPath), 0u);

    const std::filesystem::path notesPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_life_loss_validation_notes.txt";
    std::ofstream notesStream(notesPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(notesStream.is_open())
        << "Failed to open life-loss validation notes: " << notesPath.string();
    notesStream << "Life-loss validation artifacts.\n"
                << "Expected scripted life-drop frame: " << kExpectedBaselineLifeDropFrame << ".\n"
                << "Validation window: " << kLifeLossScreenshotStartFrame << "-"
                << kLifeLossScreenshotEndFrame << ".\n"
                << "Verify the visible death arc aligns with 0x000E = 0x0B, the last pre-drop\n"
                << "transition aligns with 0x000E = 0x06, and the life-drop/reload frame aligns\n"
                << "with 0x000E = 0x00.\n";
    notesStream.close();
    ASSERT_TRUE(notesStream.good());

    const size_t expectedScreenshotCount =
        static_cast<size_t>(kLifeLossScreenshotEndFrame - kLifeLossScreenshotStartFrame + 1u);
    EXPECT_EQ(screenshotCount, expectedScreenshotCount)
        << "Expected a contiguous screenshot sequence for the life-loss validation window.";
    EXPECT_TRUE(sawDeathAnimationBeforeLifeDrop)
        << "Expected 0x000E = 0x0B before the scripted life drop.";
    EXPECT_TRUE(sawPlayerDiesFrame)
        << "Expected 0x000E = 0x06 immediately before the scripted life drop.";
    EXPECT_TRUE(sawReloadFrameAfterLifeDrop)
        << "Expected 0x000E = 0x00 after the scripted life drop.";

    std::cout << "Wrote SMB life-loss validation CSV: " << csvPath.string() << "\n";
    std::cout << "Wrote SMB life-loss validation notes: " << notesPath.string() << "\n";
}

TEST(NesSuperMarioBrosRamProbeTest, ScriptedStandingJump_UsesFloatStateForAirborneSignal)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), false, ProbeScriptType::StandingJump);
    ASSERT_FALSE(frames.empty());

    const size_t analysisStartIndex = kSetupScriptEndFrame < frames.size()
        ? static_cast<size_t>(kSetupScriptEndFrame)
        : (frames.size() - 1u);
    const uint8_t baselinePlayerY = frames[analysisStartIndex].cpuRam[kPlayerYScreenAddr];

    bool sawVisibleJump = false;
    bool sawFloatStateJump = false;
    bool sawVisibleJumpWithFloatState = false;
    bool sawVisibleJumpWhilePlayerStateStayedNormal = false;

    for (size_t frameIndex = analysisStartIndex; frameIndex < frames.size(); ++frameIndex) {
        const CapturedFrame& frame = frames[frameIndex];
        const bool visuallyAirborne = frame.cpuRam[kPlayerYScreenAddr] < baselinePlayerY;
        const bool floatStateJump = frame.cpuRam[kPlayerFloatStateAddr] == 0x01u;
        const bool playerStateStillNormal = frame.cpuRam[kPlayerStateAddr] == 0x08u;

        sawVisibleJump |= visuallyAirborne;
        sawFloatStateJump |= floatStateJump;
        sawVisibleJumpWithFloatState |= visuallyAirborne && floatStateJump;
        sawVisibleJumpWhilePlayerStateStayedNormal |= visuallyAirborne && playerStateStillNormal;
    }

    EXPECT_TRUE(sawVisibleJump) << "Expected scripted standing jump to visibly leave the ground.";
    EXPECT_TRUE(sawFloatStateJump)
        << "Expected 0x001D to report jump float-state during scripted standing jump.";
    EXPECT_TRUE(sawVisibleJumpWithFloatState)
        << "Expected visually airborne frames to overlap with 0x001D jump float-state.";
    EXPECT_TRUE(sawVisibleJumpWhilePlayerStateStayedNormal)
        << "Expected visually airborne frames while 0x000E remained the normal gameplay state.";
}

TEST(NesSuperMarioBrosRamProbeTest, ScriptedLifeLoss_UsesPlayerStateDeathSequence)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), false, ProbeScriptType::Baseline);
    ASSERT_FALSE(frames.empty());

    const size_t analysisStartIndex = kSetupScriptEndFrame < frames.size()
        ? static_cast<size_t>(kSetupScriptEndFrame)
        : (frames.size() - 1u);
    const std::optional<size_t> firstLifeDropFrameIndex =
        findFirstLifeDropFrameIndex(frames, analysisStartIndex);
    ASSERT_TRUE(firstLifeDropFrameIndex.has_value()) << "Expected scripted run to lose a life.";

    const size_t lifeDropFrameIndex = firstLifeDropFrameIndex.value();
    ASSERT_GE(lifeDropFrameIndex, analysisStartIndex + 2u);
    EXPECT_EQ(frames[lifeDropFrameIndex].frameIndex, kExpectedBaselineLifeDropFrame);

    bool sawDeathAnimationBeforeLifeDrop = false;
    for (size_t frameIndex = analysisStartIndex; frameIndex < lifeDropFrameIndex; ++frameIndex) {
        sawDeathAnimationBeforeLifeDrop |= frames[frameIndex].cpuRam[kPlayerStateAddr] == 0x0Bu;
        EXPECT_NE(frames[frameIndex].cpuRam[kPlayerStateAddr], 0x00u)
            << "Did not expect 0x000E = 0x00 before the scripted life drop.";
    }

    EXPECT_TRUE(sawDeathAnimationBeforeLifeDrop)
        << "Expected 0x000E = 0x0B during the visible death animation.";
    EXPECT_EQ(frames[lifeDropFrameIndex - 1u].cpuRam[kPlayerStateAddr], 0x06u)
        << "Expected 0x000E = 0x06 on the last frame before the scripted life drop.";
    EXPECT_EQ(frames[lifeDropFrameIndex].cpuRam[kPlayerStateAddr], 0x00u)
        << "Expected 0x000E = 0x00 on the scripted life-drop frame.";
    EXPECT_EQ(frames[lifeDropFrameIndex - 1u].cpuRam[kLivesAddr], 2u);
    EXPECT_EQ(frames[lifeDropFrameIndex].cpuRam[kLivesAddr], 1u);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState deathAnimationState =
        extractor.extract(makeMemorySnapshot(frames[lifeDropFrameIndex - 2u]), true);
    const NesSuperMarioBrosState playerDiesState =
        extractor.extract(makeMemorySnapshot(frames[lifeDropFrameIndex - 1u]), true);
    const NesSuperMarioBrosState reloadState =
        extractor.extract(makeMemorySnapshot(frames[lifeDropFrameIndex]), true);

    EXPECT_EQ(frames[lifeDropFrameIndex - 2u].cpuRam[kPlayerStateAddr], 0x0Bu);
    EXPECT_EQ(deathAnimationState.lifeState, SmbLifeState::Dying);
    EXPECT_EQ(playerDiesState.lifeState, SmbLifeState::Dying);
    EXPECT_EQ(reloadState.lifeState, SmbLifeState::Dead);
}

TEST(NesSuperMarioBrosRamProbeTest, ManualStep_WritesEnemyValidationArtifacts)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), true, ProbeScriptType::EnemyValidation);
    ASSERT_FALSE(frames.empty());

    const std::filesystem::path csvPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_enemy_validation.csv";
    std::ofstream csvStream(csvPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(csvStream.is_open()) << "Failed to open enemy validation CSV: " << csvPath.string();

    csvStream
        << "frame,controller_mask,controller_label,absolute_x,player_y_screen,active_enemy_count,"
           "extractor_enemy_present,extractor_nearest_dx,extractor_nearest_dy,"
           "extractor_second_dx,extractor_second_dy,slot0_active,slot0_type,slot0_dx,slot0_dy,"
           "slot1_active,slot1_type,slot1_dx,slot1_dy,slot2_active,slot2_type,slot2_dx,slot2_dy,"
           "slot3_active,slot3_type,slot3_dx,slot3_dy,slot4_active,slot4_type,slot4_dx,slot4_dy,"
           "screenshot\n";

    NesSuperMarioBrosRamExtractor extractor;
    std::optional<uint64_t> firstEnemyFrame = std::nullopt;
    size_t screenshotCount = 0u;

    for (const CapturedFrame& frame : frames) {
        if (frame.frameIndex < kSetupScriptEndFrame) {
            continue;
        }

        const std::vector<DecodedEnemySlot> decoded = decodeActiveEnemySlots(frame);
        if (!firstEnemyFrame.has_value() && !decoded.empty()) {
            firstEnemyFrame = frame.frameIndex;
        }

        const bool selectedFrame =
            shouldCaptureScreenshot(ProbeScriptType::EnemyValidation, frame.frameIndex);
        if (!selectedFrame) {
            continue;
        }

        const NesSuperMarioBrosState state = extractor.extract(makeMemorySnapshot(frame), true);
        const std::filesystem::path screenshotPath =
            screenshotPathForFrame(ProbeScriptType::EnemyValidation, frame.frameIndex);
        if (std::filesystem::exists(screenshotPath)) {
            ++screenshotCount;
        }

        csvStream << frame.frameIndex << "," << static_cast<uint32_t>(frame.controllerMask) << ","
                  << controllerMaskToString(frame.controllerMask) << ","
                  << decodeAbsoluteX(frame.cpuRam) << ","
                  << static_cast<uint32_t>(frame.cpuRam[kPlayerYScreenAddr]) << ","
                  << decoded.size() << "," << (state.enemyPresent ? 1 : 0) << ","
                  << state.nearestEnemyDx << "," << state.nearestEnemyDy << ","
                  << state.secondNearestEnemyDx << "," << state.secondNearestEnemyDy;
        for (size_t slot = 0; slot < kEnemySlotCount; ++slot) {
            auto it =
                std::find_if(decoded.begin(), decoded.end(), [slot](const DecodedEnemySlot& enemy) {
                    return enemy.slot == slot;
                });
            if (it == decoded.end()) {
                csvStream << ",0,0,0,0";
                continue;
            }
            csvStream << ",1," << static_cast<uint32_t>(it->type) << "," << it->dx << "," << it->dy;
        }
        csvStream << "," << screenshotPath.filename().string() << "\n";
    }

    csvStream.close();
    ASSERT_TRUE(csvStream.good());
    EXPECT_TRUE(std::filesystem::exists(csvPath));
    EXPECT_GT(std::filesystem::file_size(csvPath), 0u);

    const std::filesystem::path notesPath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_enemy_validation_notes.txt";
    std::ofstream notesStream(notesPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(notesStream.is_open())
        << "Failed to open enemy validation notes: " << notesPath.string();
    notesStream << "Enemy validation artifacts for early 1-1 goombas.\n"
                << "Selected frames:";
    for (const uint64_t frame : kEnemyValidationScreenshotFrames) {
        notesStream << " " << frame;
    }
    notesStream << "\n";
    if (firstEnemyFrame.has_value()) {
        notesStream << "First active enemy frame: " << firstEnemyFrame.value()
                    << " (slot active, not necessarily first clearly visible frame).\n";
    }
    notesStream
        << "Compare screenshots against active enemy counts and relative dx values in the CSV.\n";
    notesStream.close();
    ASSERT_TRUE(notesStream.good());

    EXPECT_TRUE(firstEnemyFrame.has_value())
        << "Expected the baseline run to encounter early goombas.";
    EXPECT_EQ(firstEnemyFrame.value_or(0u), kExpectedFirstEnemyActiveFrame);
    EXPECT_EQ(screenshotCount, kEnemyValidationScreenshotFrames.size())
        << "Expected a screenshot for every selected enemy validation frame.";

    std::cout << "Wrote SMB enemy validation CSV: " << csvPath.string() << "\n";
    std::cout << "Wrote SMB enemy validation notes: " << notesPath.string() << "\n";
}

TEST(NesSuperMarioBrosRamProbeTest, ScriptedBaseline_EnemyWindowsMatchGoombaProgression)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), false, ProbeScriptType::EnemyValidation);
    ASSERT_FALSE(frames.empty());

    std::optional<size_t> firstEnemyFrameIndex = std::nullopt;
    for (size_t frameIndex = static_cast<size_t>(kSetupScriptEndFrame); frameIndex < frames.size();
         ++frameIndex) {
        if (!decodeActiveEnemySlots(frames[frameIndex]).empty()) {
            firstEnemyFrameIndex = frameIndex;
            break;
        }
    }

    ASSERT_TRUE(firstEnemyFrameIndex.has_value()) << "Expected an active goomba in early 1-1.";
    EXPECT_EQ(frames[firstEnemyFrameIndex.value()].frameIndex, kExpectedFirstEnemyActiveFrame);

    const auto findFrame = [&frames](uint64_t frameNumber) -> const CapturedFrame& {
        auto it =
            std::find_if(frames.begin(), frames.end(), [frameNumber](const CapturedFrame& frame) {
                return frame.frameIndex == frameNumber;
            });
        EXPECT_TRUE(it != frames.end()) << "Missing captured frame " << frameNumber;
        return *it;
    };

    const CapturedFrame& frame500 = findFrame(500u);
    const CapturedFrame& frame700 = findFrame(700u);
    const CapturedFrame& frame899 = findFrame(899u);

    const std::vector<DecodedEnemySlot> enemies500 = decodeActiveEnemySlots(frame500);
    const std::vector<DecodedEnemySlot> enemies700 = decodeActiveEnemySlots(frame700);
    const std::vector<DecodedEnemySlot> enemies899 = decodeActiveEnemySlots(frame899);

    ASSERT_EQ(enemies500.size(), 1u);
    ASSERT_EQ(enemies700.size(), 1u);
    ASSERT_EQ(enemies899.size(), 3u);

    EXPECT_EQ(enemies500[0].type, 6u);
    EXPECT_EQ(enemies700[0].type, 6u);
    EXPECT_EQ(enemies899[0].type, 6u);
    EXPECT_EQ(enemies899[1].type, 6u);
    EXPECT_EQ(enemies899[2].type, 6u);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state500 = extractor.extract(makeMemorySnapshot(frame500), true);
    const NesSuperMarioBrosState state700 = extractor.extract(makeMemorySnapshot(frame700), true);
    const NesSuperMarioBrosState state899 = extractor.extract(makeMemorySnapshot(frame899), true);

    EXPECT_TRUE(state500.enemyPresent);
    EXPECT_EQ(state500.nearestEnemyDx, enemies500[0].dx);
    EXPECT_EQ(state500.nearestEnemyDy, enemies500[0].dy);

    EXPECT_TRUE(state700.enemyPresent);
    EXPECT_EQ(state700.nearestEnemyDx, enemies700[0].dx);
    EXPECT_EQ(state700.nearestEnemyDy, enemies700[0].dy);

    EXPECT_TRUE(state899.enemyPresent);
    EXPECT_EQ(state899.nearestEnemyDx, enemies899[0].dx);
    EXPECT_EQ(state899.nearestEnemyDy, enemies899[0].dy);
    EXPECT_EQ(state899.secondNearestEnemyDx, enemies899[1].dx);
    EXPECT_EQ(state899.secondNearestEnemyDy, enemies899[1].dy);
}

TEST(NesSuperMarioBrosRamProbeTest, ManualStep_ValidatesPlayerOneButtonMaskRegisters)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romId = "";
    config.romPath = romPath->string();
    config.requireSmolnesMapper = true;

    constexpr double deltaTimeSeconds = 1.0 / 60.0;
    const std::vector<NesRamProbeAddress> cpuAddresses = {
        NesRamProbeAddress{ .label = "p1_buttons", .address = kP1ButtonsPressedAddr },
        NesRamProbeAddress{ .label = "p2_buttons", .address = kP2ButtonsPressedAddr },
        NesRamProbeAddress{ .label = "duck_state", .address = 0x0714 },
        NesRamProbeAddress{ .label = "player_state", .address = kPlayerStateAddr },
        NesRamProbeAddress{ .label = "powerup_state", .address = kPowerupStateAddr },
    };
    constexpr size_t kP1ButtonsIndex = 0u;
    constexpr size_t kP2ButtonsIndex = 1u;
    constexpr uint64_t kWindowFrames = 8u;

    struct ProbeWindow {
        const char* label = "";
        uint8_t controllerMask = 0;
    };

    const std::vector<ProbeWindow> windows = {
        ProbeWindow{ .label = "Neutral0", .controllerMask = 0u },
        ProbeWindow{ .label = "Right", .controllerMask = SMOLNES_RUNTIME_BUTTON_RIGHT },
        ProbeWindow{ .label = "Neutral1", .controllerMask = 0u },
        ProbeWindow{ .label = "Left", .controllerMask = SMOLNES_RUNTIME_BUTTON_LEFT },
        ProbeWindow{ .label = "Neutral2", .controllerMask = 0u },
        ProbeWindow{ .label = "Down", .controllerMask = SMOLNES_RUNTIME_BUTTON_DOWN },
        ProbeWindow{ .label = "Neutral3", .controllerMask = 0u },
        ProbeWindow{ .label = "Up", .controllerMask = SMOLNES_RUNTIME_BUTTON_UP },
        ProbeWindow{ .label = "Neutral4", .controllerMask = 0u },
        ProbeWindow{ .label = "A", .controllerMask = SMOLNES_RUNTIME_BUTTON_A },
        ProbeWindow{ .label = "Neutral5", .controllerMask = 0u },
        ProbeWindow{ .label = "B", .controllerMask = SMOLNES_RUNTIME_BUTTON_B },
        ProbeWindow{ .label = "Neutral6", .controllerMask = 0u },
        ProbeWindow{ .label = "Start", .controllerMask = SMOLNES_RUNTIME_BUTTON_START },
        ProbeWindow{ .label = "Neutral7", .controllerMask = 0u },
        ProbeWindow{ .label = "Select", .controllerMask = SMOLNES_RUNTIME_BUTTON_SELECT },
        ProbeWindow{
            .label = "A|Right",
            .controllerMask =
                static_cast<uint8_t>(SMOLNES_RUNTIME_BUTTON_A | SMOLNES_RUNTIME_BUTTON_RIGHT),
        },
        ProbeWindow{
            .label = "Left|Down",
            .controllerMask =
                static_cast<uint8_t>(SMOLNES_RUNTIME_BUTTON_LEFT | SMOLNES_RUNTIME_BUTTON_DOWN),
        },
    };

    struct ObservedWindow {
        std::string label;
        uint8_t controllerMask = 0;
        std::vector<NesRamProbeFrame> frames;
    };

    NesRamProbeStepper stepper(
        Scenario::EnumType::NesSuperMarioBros,
        ScenarioConfig{ config },
        cpuAddresses,
        deltaTimeSeconds);
    ASSERT_TRUE(stepper.isRuntimeReady()) << stepper.getLastError();

    for (uint64_t frameIndex = 0; frameIndex < kSetupScriptEndFrame; ++frameIndex) {
        (void)stepper.step(getNesSuperMarioBrosScriptedSetupMaskForFrame(frameIndex));
    }
    ASSERT_TRUE(stepper.isRuntimeReady()) << stepper.getLastError();

    std::vector<ObservedWindow> observedWindows;
    observedWindows.reserve(windows.size());
    for (const ProbeWindow& window : windows) {
        ObservedWindow observedWindow;
        observedWindow.label = window.label;
        observedWindow.controllerMask = window.controllerMask;
        observedWindow.frames.reserve(static_cast<size_t>(kWindowFrames));
        for (uint64_t step = 0; step < kWindowFrames; ++step) {
            observedWindow.frames.push_back(stepper.step(window.controllerMask));
        }
        observedWindows.push_back(std::move(observedWindow));
    }

    const std::filesystem::path tracePath =
        std::filesystem::path(::testing::TempDir()) / "nes_smb_input_mask_probe.csv";
    std::ofstream traceStream(tracePath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(traceStream.is_open())
        << "Failed to open SMB input-mask probe trace: " << tracePath.string();
    traceStream << "window,frame,controller_mask,cpu_0x074A,cpu_0x074B,cpu_0x0714,cpu_0x000E,"
                   "cpu_0x0756\n";
    for (const ObservedWindow& observedWindow : observedWindows) {
        for (const NesRamProbeFrame& frame : observedWindow.frames) {
            ASSERT_GE(frame.cpuRamValues.size(), cpuAddresses.size());
            traceStream << observedWindow.label << "," << frame.frame << ","
                        << static_cast<uint32_t>(frame.controllerMask) << ","
                        << static_cast<uint32_t>(frame.cpuRamValues[kP1ButtonsIndex]) << ","
                        << static_cast<uint32_t>(frame.cpuRamValues[kP2ButtonsIndex]) << ","
                        << static_cast<uint32_t>(frame.cpuRamValues[2]) << ","
                        << static_cast<uint32_t>(frame.cpuRamValues[3]) << ","
                        << static_cast<uint32_t>(frame.cpuRamValues[4]) << "\n";
        }
    }
    traceStream.close();
    ASSERT_TRUE(traceStream.good());
    EXPECT_TRUE(std::filesystem::exists(tracePath));
    EXPECT_GT(std::filesystem::file_size(tracePath), 0u);
    std::cout << "Wrote SMB input-mask probe trace: " << tracePath.string() << "\n";

    const auto modalRamValue = [](const std::vector<NesRamProbeFrame>& frames,
                                  size_t valueIndex) -> uint8_t {
        std::array<uint32_t, 256> counts{};
        for (const NesRamProbeFrame& frame : frames) {
            counts[frame.cpuRamValues[valueIndex]]++;
        }

        size_t bestValue = 0u;
        uint32_t bestCount = 0u;
        for (size_t value = 0; value < counts.size(); ++value) {
            if (counts[value] > bestCount) {
                bestCount = counts[value];
                bestValue = value;
            }
        }
        return static_cast<uint8_t>(bestValue);
    };

    std::map<std::string, uint8_t> stableP1Values;
    std::map<std::string, uint8_t> stableP2Values;
    for (const ObservedWindow& observedWindow : observedWindows) {
        stableP1Values[observedWindow.label] =
            modalRamValue(observedWindow.frames, kP1ButtonsIndex);
        stableP2Values[observedWindow.label] =
            modalRamValue(observedWindow.frames, kP2ButtonsIndex);
    }

    for (const auto& [label, value] : stableP2Values) {
        EXPECT_EQ(value, 0u) << "Expected 0x074B to remain zero for P1-only probe at " << label;
    }

    EXPECT_EQ(stableP1Values["Neutral0"], 0u);
    EXPECT_EQ(stableP1Values["Neutral1"], 0u);
    EXPECT_EQ(stableP1Values["Neutral2"], 0u);
    EXPECT_EQ(stableP1Values["Neutral3"], 0u);
    EXPECT_EQ(stableP1Values["Neutral4"], 0u);
    EXPECT_EQ(stableP1Values["Neutral5"], 0u);
    EXPECT_EQ(stableP1Values["Neutral6"], 0u);
    EXPECT_EQ(stableP1Values["Neutral7"], 0u);

    const std::vector<std::string> singleButtonLabels = { "Right", "Left", "Down",  "Up",
                                                          "A",     "B",    "Start", "Select" };
    std::set<uint8_t> singleButtonValues;
    for (const std::string& label : singleButtonLabels) {
        const uint8_t stableValue = stableP1Values[label];
        EXPECT_NE(stableValue, 0u) << "Expected 0x074A to change during window " << label;
        EXPECT_TRUE(std::has_single_bit(static_cast<uint32_t>(stableValue)))
            << "Expected 0x074A to expose a single-bit mask for " << label << ", got "
            << static_cast<uint32_t>(stableValue);
        singleButtonValues.insert(stableValue);
    }
    EXPECT_EQ(singleButtonValues.size(), singleButtonLabels.size())
        << "Expected each single button to map to a distinct 0x074A bit.";

    EXPECT_EQ(
        stableP1Values["A|Right"],
        static_cast<uint8_t>(stableP1Values["A"] | stableP1Values["Right"]));
    EXPECT_EQ(
        stableP1Values["Left|Down"],
        static_cast<uint8_t>(stableP1Values["Left"] | stableP1Values["Down"]));

    std::cout << "0x074A inferred button values:";
    for (const std::string& label : singleButtonLabels) {
        std::cout << " " << label << "=" << static_cast<uint32_t>(stableP1Values[label]);
    }
    std::cout << " A|Right=" << static_cast<uint32_t>(stableP1Values["A|Right"])
              << " Left|Down=" << static_cast<uint32_t>(stableP1Values["Left|Down"]) << "\n";
}

TEST(NesSuperMarioBrosRamProbeTest, ReplayThroughExtractorAndEvaluatorMatchesLiveRun)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    const std::vector<CapturedFrame> frames =
        captureSmbProbeFrames(romPath.value(), false, ProbeScriptType::Baseline);
    ASSERT_FALSE(frames.empty());

    const size_t analysisStartIndex = kSetupScriptEndFrame < frames.size()
        ? static_cast<size_t>(kSetupScriptEndFrame)
        : (frames.size() - 1u);

    const size_t lifeDropCount = countLifeDrops(frames, analysisStartIndex);
    const std::optional<size_t> firstLifeDropFrameIndex =
        findFirstLifeDropFrameIndex(frames, analysisStartIndex);

    ASSERT_EQ(lifeDropCount, 1u);
    ASSERT_TRUE(firstLifeDropFrameIndex.has_value());

    const SmbProbeReplaySummary summary = replaySmbProbeFramesThroughExtractorAndEvaluator(frames);

    EXPECT_GT(summary.gameplayFrameCount, 0u);
    EXPECT_GT(summary.aliveGameplayFrameCount, 0u)
        << "Expected the extractor to classify at least some live gameplay frames as Alive.";
    EXPECT_GE(summary.maxAbsoluteX, 512u)
        << "Expected the live replay to carry extracted absoluteX past two x-page boundaries.";
    EXPECT_GT(summary.rewardTotal, 0.0)
        << "Expected the evaluator to reward forward progress during the live scripted run.";
    EXPECT_GT(summary.positiveRewardFrameCount, 0u)
        << "Expected the live replay to produce at least one positive reward frame.";
    ASSERT_TRUE(summary.firstDoneFrame.has_value());
    EXPECT_EQ(summary.firstDoneFrame.value(), frames[firstLifeDropFrameIndex.value()].frameIndex)
        << "Expected the evaluator to terminate on the first life loss in the live replay.";
}
