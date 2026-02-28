#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/scenarios/NesSuperTiltBroScenario.h"
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
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using namespace DirtSim;

namespace {

constexpr double kFrameDeltaSeconds = 1.0 / 60.0;

std::optional<std::filesystem::path> resolveNesStbFixtureRomPath()
{
    if (const char* romPathEnv = std::getenv("DIRTSIM_NES_STB_TEST_ROM_PATH");
        romPathEnv != nullptr) {
        const std::filesystem::path romPath{ romPathEnv };
        if (std::filesystem::exists(romPath)) {
            return romPath;
        }
    }

    const std::filesystem::path repoRelativeRomPath =
        std::filesystem::path("testdata") / "roms" / "tilt_no_network_unrom_(E).nes";
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

uint8_t scriptedSetupMaskForFrame(uint64_t frameIndex)
{
    constexpr uint64_t kBootWaitFrames = 120;
    if (frameIndex < kBootWaitFrames) {
        return 0u;
    }

    constexpr uint64_t kStartPressWidthFrames = 1;
    constexpr std::array<uint64_t, 6> kStartPressFrames = { 120u, 240u, 360u, 480u, 1000u, 1120u };
    for (const uint64_t pressFrame : kStartPressFrames) {
        if (frameIndex >= pressFrame && frameIndex < (pressFrame + kStartPressWidthFrames)) {
            return SMOLNES_RUNTIME_BUTTON_START;
        }
    }

    return 0u;
}

uint8_t scriptedMatchMaskForFrame(uint64_t frameIndex)
{
    constexpr uint64_t kMatchStartFrame = 1200;
    if (frameIndex < kMatchStartFrame) {
        return 0u;
    }

    const uint64_t matchFrame = frameIndex - kMatchStartFrame;
    if (matchFrame < 60) {
        return 0u;
    }

    const uint64_t t = matchFrame - 60;

    uint8_t mask = 0u;
    if (((t / 180) % 2) == 0) {
        mask |= SMOLNES_RUNTIME_BUTTON_RIGHT;
    }
    else {
        mask |= SMOLNES_RUNTIME_BUTTON_LEFT;
    }

    if ((t % 10) == 0) {
        mask |= SMOLNES_RUNTIME_BUTTON_B;
    }

    if ((t % 60) == 30) {
        mask |= SMOLNES_RUNTIME_BUTTON_A;
    }

    return mask;
}

uint8_t scriptedControllerMaskForFrame(uint64_t frameIndex)
{
    const uint8_t setupMask = scriptedSetupMaskForFrame(frameIndex);
    if (setupMask != 0u) {
        return setupMask;
    }

    return scriptedMatchMaskForFrame(frameIndex);
}

struct CapturedFrame {
    uint64_t frameIndex = 0;
    uint8_t controllerMask = 0;
    std::vector<uint8_t> cpuRam;
};

} // namespace

TEST(NesSuperTiltBroRamProbeTest, ManualStep_WritesCandidateRamTraceCsv)
{
    const std::optional<std::filesystem::path> romPath = resolveNesStbFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom --all' or "
                        "set DIRTSIM_NES_STB_TEST_ROM_PATH.";
    }

    auto scenario = std::make_unique<NesSuperTiltBroScenario>();
    const ScenarioMetadata& metadata = scenario->getMetadata();
    World world(metadata.requiredWidth, metadata.requiredHeight);

    Config::NesSuperTiltBro config = std::get<Config::NesSuperTiltBro>(scenario->getConfig());
    config.romId = "";
    config.romPath = romPath->string();
    config.requireSmolnesMapper = true;
    scenario->setConfig(config, world);
    scenario->setup(world);

    ASSERT_TRUE(scenario->isRuntimeRunning()) << scenario->getRuntimeLastError();
    ASSERT_TRUE(scenario->isRuntimeHealthy()) << scenario->getRuntimeLastError();

    constexpr uint64_t kCaptureFrames = 1801;
    const std::array<uint64_t, 19> screenshotFrames = {
        0u,    120u,  240u,  360u,  480u,  600u,  720u,  1000u, 1040u, 1100u,
        1120u, 1200u, 1320u, 1380u, 1440u, 1560u, 1680u, 1740u, 1800u,
    };

    const std::filesystem::path tempDir = ::testing::TempDir();
    for (const auto& entry : std::filesystem::directory_iterator(tempDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        if (filename.rfind("nes_stb_frame_", 0) == 0 && entry.path().extension() == ".ppm") {
            std::error_code ec;
            std::filesystem::remove(entry.path(), ec);
        }
    }

    std::vector<CapturedFrame> frames;
    frames.reserve(static_cast<size_t>(kCaptureFrames));

    for (uint64_t frameIndex = 0; frameIndex < kCaptureFrames; ++frameIndex) {
        const uint8_t controllerMask = scriptedControllerMaskForFrame(frameIndex);
        scenario->setController1State(controllerMask);
        scenario->tick(world, kFrameDeltaSeconds);

        for (uint64_t snapshotFrame : screenshotFrames) {
            if (snapshotFrame != frameIndex) {
                continue;
            }

            const auto scenarioFrame = scenario->copyRuntimeFrameSnapshot();
            if (!scenarioFrame.has_value()) {
                continue;
            }

            const std::filesystem::path screenshotPath = std::filesystem::path(::testing::TempDir())
                / ("nes_stb_frame_" + std::to_string(frameIndex) + ".ppm");
            if (writeScenarioFramePpm(scenarioFrame.value(), screenshotPath)) {
                std::cout << "Wrote STB screenshot: " << screenshotPath.string() << "\n";
            }
        }

        const auto snapshot = scenario->copyRuntimeMemorySnapshot();
        ASSERT_TRUE(snapshot.has_value());

        CapturedFrame frame;
        frame.frameIndex = frameIndex;
        frame.controllerMask = controllerMask;
        frame.cpuRam.assign(snapshot->cpuRam.begin(), snapshot->cpuRam.end());
        frames.push_back(std::move(frame));
    }

    ASSERT_EQ(scenario->getRuntimeRenderedFrameCount(), kCaptureFrames);
    ASSERT_FALSE(frames.empty());
    ASSERT_EQ(frames.front().cpuRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_CPU_RAM_BYTES));

    std::vector<uint32_t> changeCounts(frames.front().cpuRam.size(), 0);
    constexpr uint64_t kMatchAnalysisStartFrame = 1200;
    const size_t analysisStartIndex = kMatchAnalysisStartFrame < frames.size()
        ? static_cast<size_t>(kMatchAnalysisStartFrame)
        : (frames.size() - 1u);

    for (size_t frameIndex = analysisStartIndex + 1u; frameIndex < frames.size(); ++frameIndex) {
        const std::vector<uint8_t>& previous = frames[frameIndex - 1].cpuRam;
        const std::vector<uint8_t>& current = frames[frameIndex].cpuRam;
        ASSERT_EQ(previous.size(), current.size());
        for (size_t addr = 0; addr < current.size(); ++addr) {
            if (current[addr] != previous[addr]) {
                changeCounts[addr]++;
            }
        }
    }

    std::vector<size_t> rankedAddresses(changeCounts.size(), 0);
    std::iota(rankedAddresses.begin(), rankedAddresses.end(), 0);
    std::sort(
        rankedAddresses.begin(), rankedAddresses.end(), [&changeCounts](size_t lhs, size_t rhs) {
            return changeCounts[lhs] > changeCounts[rhs];
        });

    const size_t candidateCount = std::min<size_t>(64, rankedAddresses.size());
    rankedAddresses.resize(candidateCount);

    const std::filesystem::path tracePath =
        std::filesystem::path(::testing::TempDir()) / "nes_stb_ram_probe_candidates.csv";
    std::ofstream stream(tracePath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(stream.is_open()) << "Failed to open probe trace: " << tracePath.string();

    stream << "frame,controller_mask";
    for (size_t addr : rankedAddresses) {
        stream << ",cpu_" << addr;
    }
    stream << "\n";

    for (const CapturedFrame& frame : frames) {
        if (frame.frameIndex < kMatchAnalysisStartFrame) {
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

    std::cout << "Wrote STB RAM probe trace: " << tracePath.string() << "\n";
    std::cout << "Top changed address: cpu_" << rankedAddresses.front() << " (changed "
              << changeCounts[rankedAddresses.front()] << " times)\n";
}
