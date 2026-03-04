#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
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
#include <string>
#include <vector>

using namespace DirtSim;

namespace {

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

uint8_t scriptedControllerMaskForFrame(uint64_t frameIndex)
{
    constexpr uint64_t kStartPressWidthFrames = 1;
    constexpr std::array<uint64_t, 2> kStartPressFrames = { 120u, 240u };
    for (const uint64_t pressFrame : kStartPressFrames) {
        if (frameIndex >= pressFrame && frameIndex < (pressFrame + kStartPressWidthFrames)) {
            return SMOLNES_RUNTIME_BUTTON_START;
        }
    }

    constexpr uint64_t kGameplayStartFrame = 300;
    if (frameIndex < kGameplayStartFrame) {
        return 0u;
    }

    const uint64_t gameFrame = frameIndex - kGameplayStartFrame;
    uint8_t mask = SMOLNES_RUNTIME_BUTTON_RIGHT;
    if (gameFrame % 60 < 15) {
        mask |= SMOLNES_RUNTIME_BUTTON_A;
    }

    return mask;
}

struct CapturedFrame {
    uint64_t frameIndex = 0;
    uint8_t controllerMask = 0;
    std::vector<uint8_t> cpuRam;
};

} // namespace

TEST(NesSuperMarioBrosRamProbeTest, ManualStep_WritesCandidateRamTraceCsv)
{
    const std::optional<std::filesystem::path> romPath = resolveNesSmbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_SMB_TEST_ROM_PATH or place "
                        "smb.nes in testdata/roms/.";
    }

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesSuperMarioBros);
    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romId = "";
    config.romPath = romPath->string();
    config.requireSmolnesMapper = true;
    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    ASSERT_TRUE(setResult.isValue()) << setResult.errorValue();
    const auto setupResult = driver.setup();
    ASSERT_TRUE(setupResult.isValue()) << setupResult.errorValue();

    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();
    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();
    Timers timers;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;

    constexpr uint64_t kCaptureFrames = 1100;
    const std::array<uint64_t, 14> screenshotFrames = {
        0u, 120u, 240u, 300u, 400u, 500u, 600u, 700u, 800u, 899u, 950u, 1000u, 1050u, 1099u,
    };

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

    std::vector<CapturedFrame> frames;
    frames.reserve(static_cast<size_t>(kCaptureFrames));

    for (uint64_t frameIndex = 0; frameIndex < kCaptureFrames; ++frameIndex) {
        const uint8_t controllerMask = scriptedControllerMaskForFrame(frameIndex);
        driver.setController1State(controllerMask);
        driver.tick(timers, scenarioVideoFrame);

        for (uint64_t snapshotFrame : screenshotFrames) {
            if (snapshotFrame != frameIndex) {
                continue;
            }

            const auto scenarioFrame = driver.copyRuntimeFrameSnapshot();
            if (!scenarioFrame.has_value()) {
                continue;
            }

            const std::filesystem::path screenshotPath = std::filesystem::path(::testing::TempDir())
                / ("nes_smb_frame_" + std::to_string(frameIndex) + ".ppm");
            if (writeScenarioFramePpm(scenarioFrame.value(), screenshotPath)) {
                std::cout << "Wrote SMB screenshot: " << screenshotPath.string() << "\n";
            }
        }

        const auto snapshot = driver.copyRuntimeMemorySnapshot();
        ASSERT_TRUE(snapshot.has_value());

        CapturedFrame frame;
        frame.frameIndex = frameIndex;
        frame.controllerMask = controllerMask;
        frame.cpuRam.assign(snapshot->cpuRam.begin(), snapshot->cpuRam.end());
        frames.push_back(frame);
    }

    ASSERT_EQ(driver.getRuntimeRenderedFrameCount(), kCaptureFrames);
    ASSERT_FALSE(frames.empty());
    ASSERT_EQ(frames.front().cpuRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_CPU_RAM_BYTES));

    constexpr uint64_t kGameplayAnalysisStartFrame = 300;
    const size_t analysisStartIndex = kGameplayAnalysisStartFrame < frames.size()
        ? static_cast<size_t>(kGameplayAnalysisStartFrame)
        : (frames.size() - 1u);

    const std::vector<uint8_t>& gameplayStartRam = frames[analysisStartIndex].cpuRam;
    const uint8_t gameEngineAtStart = gameplayStartRam[0x0770];
    std::cout << "Game engine at frame " << kGameplayAnalysisStartFrame << ": "
              << static_cast<uint32_t>(gameEngineAtStart) << "\n";

    bool foundGameplay = false;
    bool playerXChanged = false;
    uint8_t firstGameplayX = 0;
    for (size_t i = analysisStartIndex; i < frames.size(); ++i) {
        const uint8_t engine = frames[i].cpuRam[0x0770];
        if (engine == 1) {
            foundGameplay = true;
            const uint8_t lives = frames[i].cpuRam[0x075A];
            EXPECT_GT(lives, 0u) << "Lives should be positive during gameplay at frame " << i;

            const uint8_t world = frames[i].cpuRam[0x075F];
            const uint8_t level = frames[i].cpuRam[0x0760];
            if (i == analysisStartIndex) {
                EXPECT_EQ(world, 0u);
                EXPECT_EQ(level, 0u);
            }

            const uint8_t playerX = frames[i].cpuRam[0x0086];
            if (i == analysisStartIndex) {
                firstGameplayX = playerX;
            }
            else if (playerX != firstGameplayX) {
                playerXChanged = true;
            }

            const uint8_t horizontalSpeed = frames[i].cpuRam[0x0057];
            if (playerXChanged && horizontalSpeed > 0) {
                const uint8_t powerup = frames[i].cpuRam[0x0756];
                EXPECT_EQ(powerup, 0u)
                    << "Powerup should be 0 (small) at world 1-1 start, frame " << i;
                break;
            }
        }
    }

    if (foundGameplay) {
        EXPECT_TRUE(playerXChanged) << "Player X should change when pressing RIGHT.";
    }

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
        if (frame.frameIndex < kGameplayAnalysisStartFrame) {
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
