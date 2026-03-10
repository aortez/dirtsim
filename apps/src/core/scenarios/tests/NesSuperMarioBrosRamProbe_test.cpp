#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"
#include "core/scenarios/nes/NesSuperMarioBrosSetupPolicy.h"
#include "core/scenarios/nes/SmolnesRuntimeBackend.h"

#include <algorithm>
#include <array>
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
constexpr size_t kPlayerFacingDirCandidateAAddr = 0x0033;
constexpr size_t kPlayerFacingDirCandidateBAddr = 0x0045;
constexpr size_t kPlayerStateAddr = 0x000E;
constexpr size_t kPlayerXPageAddr = 0x006D;
constexpr size_t kPlayerXScreenAddr = 0x0086;
constexpr size_t kPlayerYScreenAddr = 0x00CE;
constexpr size_t kPlayerXSpeedAbsoluteAddr = 0x0700;
constexpr size_t kLivesAddr = 0x075A;
constexpr size_t kPowerupStateAddr = 0x0756;
constexpr size_t kWorldAddr = 0x075F;
constexpr size_t kLevelAddr = 0x0760;
constexpr size_t kGameEngineAddr = 0x0770;
constexpr uint64_t kBaselineProbeCaptureFrames = 1100;
constexpr uint64_t kLeftMovementProbeCaptureFrames = 480;
constexpr uint64_t kSetupScriptEndFrame = 300;
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
    LeftMovement = 1,
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

bool writeScenarioFramePpm(const ScenarioVideoFrame& frame, const std::filesystem::path& path)
{
    if (frame.width == 0 || frame.height == 0) {
        return false;
    }

    const size_t expectedBytes =
        static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 2u;
    if (frame.pixels.size() != expectedBytes) {
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    stream << "P6\n" << frame.width << " " << frame.height << "\n255\n";

    const size_t pixelCount = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const std::optional<uint16_t> rgb565 = readRgb565Pixel(frame, pixelIndex);
        if (!rgb565.has_value()) {
            return false;
        }
        const std::array<uint8_t, 3> rgb = rgb565ToRgb888(rgb565.value());
        stream.write(
            reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    }

    return stream.good();
}

uint16_t decodeAbsoluteX(const std::vector<uint8_t>& cpuRam)
{
    if (kPlayerXPageAddr >= cpuRam.size() || kPlayerXScreenAddr >= cpuRam.size()) {
        return 0;
    }

    return (static_cast<uint16_t>(cpuRam[kPlayerXPageAddr]) << 8)
        | static_cast<uint16_t>(cpuRam[kPlayerXScreenAddr]);
}

uint64_t probeCaptureFrameCount(ProbeScriptType probeScriptType)
{
    switch (probeScriptType) {
        case ProbeScriptType::Baseline:
            return kBaselineProbeCaptureFrames;
        case ProbeScriptType::LeftMovement:
            return kLeftMovementProbeCaptureFrames;
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
        case ProbeScriptType::LeftMovement:
            return false;
    }

    return false;
}

uint8_t scriptedControllerMaskForFrame(uint64_t frameIndex, ProbeScriptType probeScriptType)
{
    if (frameIndex < kSetupScriptEndFrame) {
        return getNesSuperMarioBrosScriptedSetupMaskForFrame(frameIndex);
    }

    const uint64_t gameFrame = frameIndex - kSetupScriptEndFrame;
    switch (probeScriptType) {
        case ProbeScriptType::Baseline: {
            const bool gradualWalkWindow = gameFrame >= 140u && gameFrame < 220u;
            const bool pressRight = !gradualWalkWindow || ((gameFrame % 2u) == 0u);

            uint8_t mask = pressRight ? SMOLNES_RUNTIME_BUTTON_RIGHT : 0u;
            if (gameFrame % 60 < 15) {
                mask |= SMOLNES_RUNTIME_BUTTON_A;
            }

            return mask;
        }
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
        const std::filesystem::path tempDir = ::testing::TempDir();
        for (const auto& entry : std::filesystem::directory_iterator(tempDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string filename = entry.path().filename().string();
            if (filename.rfind("nes_smb_frame_", 0) == 0 && entry.path().extension() == ".ppm") {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            }
        }
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
                    std::filesystem::path(::testing::TempDir())
                    / ("nes_smb_frame_" + std::to_string(frameIndex) + ".ppm");
                if (writeScenarioFramePpm(stepResult.scenarioVideoFrame.value(), screenshotPath)) {
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
