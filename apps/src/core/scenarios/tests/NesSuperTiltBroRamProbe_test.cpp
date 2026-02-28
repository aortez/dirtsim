#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/scenarios/NesSuperTiltBroScenario.h"
#include "core/scenarios/nes/SmolnesRuntimeBackend.h"

#include <algorithm>
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

uint8_t scriptedSetupMaskForFrame(uint64_t frameIndex)
{
    constexpr uint64_t kBootWaitFrames = 120;
    if (frameIndex < kBootWaitFrames) {
        return 0u;
    }

    const uint64_t phase = (frameIndex - kBootWaitFrames) / 60;
    const uint64_t withinPhase = (frameIndex - kBootWaitFrames) % 60;

    if (withinPhase < 2) {
        return SMOLNES_RUNTIME_BUTTON_START;
    }
    if (withinPhase >= 10 && withinPhase < 12) {
        return SMOLNES_RUNTIME_BUTTON_A;
    }

    if (phase % 2 == 1 && withinPhase >= 20 && withinPhase < 34) {
        return SMOLNES_RUNTIME_BUTTON_RIGHT;
    }

    return 0u;
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

    constexpr uint64_t kCaptureFrames = 600;

    std::vector<CapturedFrame> frames;
    frames.reserve(static_cast<size_t>(kCaptureFrames));

    for (uint64_t frameIndex = 0; frameIndex < kCaptureFrames; ++frameIndex) {
        const uint8_t controllerMask = scriptedSetupMaskForFrame(frameIndex);
        scenario->setController1State(controllerMask);
        scenario->tick(world, kFrameDeltaSeconds);

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
    for (size_t frameIndex = 1; frameIndex < frames.size(); ++frameIndex) {
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

    const size_t candidateCount = std::min<size_t>(32, rankedAddresses.size());
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
