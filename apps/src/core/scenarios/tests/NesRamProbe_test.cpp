#include "core/ScenarioConfig.h"
#include "core/scenarios/nes/NesRamProbe.h"
#include "core/scenarios/nes/SmolnesRuntimeBackend.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace DirtSim;

namespace {

constexpr uint16_t kBirdXAddr = 0x20;
constexpr uint16_t kGameStateAddr = 0x0A;
constexpr double kFrameDeltaSeconds = 1.0 / 60.0;

std::optional<std::filesystem::path> resolveNesFixtureRomPath()
{
    if (const char* romPathEnv = std::getenv("DIRTSIM_NES_TEST_ROM_PATH"); romPathEnv != nullptr) {
        const std::filesystem::path romPath{ romPathEnv };
        if (std::filesystem::exists(romPath)) {
            return romPath;
        }
    }

    const std::filesystem::path repoRelativeRomPath =
        std::filesystem::path("testdata") / "roms" / "Flappy.Paratroopa.World.Unl.nes";
    if (std::filesystem::exists(repoRelativeRomPath)) {
        return repoRelativeRomPath;
    }

    return std::nullopt;
}

std::vector<uint8_t> buildScriptedControllerSequence()
{
    std::vector<uint8_t> script;
    script.reserve(361);
    script.push_back(SMOLNES_RUNTIME_BUTTON_A);
    for (int i = 0; i < 360; ++i) {
        if (i % 18 == 0 || i % 18 == 1) {
            script.push_back(SMOLNES_RUNTIME_BUTTON_A);
        }
        else {
            script.push_back(0u);
        }
    }

    return script;
}

Config::NesFlappyParatroopa makeFlappyProbeConfig(const std::filesystem::path& romPath)
{
    Config::NesFlappyParatroopa config = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    config.romPath = romPath.string();
    config.requireSmolnesMapper = true;
    return config;
}

NesRamProbeTrace runProbeTraceOnce(const std::filesystem::path& romPath)
{
    const std::vector<NesRamProbeAddress> addresses{
        NesRamProbeAddress{ .label = "game_state", .address = kGameStateAddr },
        NesRamProbeAddress{ .label = "scroll_x", .address = 0x08 },
        NesRamProbeAddress{ .label = "scroll_nt", .address = 0x09 },
        NesRamProbeAddress{ .label = "bird_y", .address = 0x01 },
        NesRamProbeAddress{ .label = "bird_vel_hi", .address = 0x03 },
        NesRamProbeAddress{ .label = "bird_x", .address = kBirdXAddr },
        NesRamProbeAddress{ .label = "score_ones", .address = 0x19 },
        NesRamProbeAddress{ .label = "score_tens", .address = 0x1A },
        NesRamProbeAddress{ .label = "score_hundreds", .address = 0x1B },
        NesRamProbeAddress{ .label = "nt0_pipe0_gap", .address = 0x12 },
        NesRamProbeAddress{ .label = "nt0_pipe1_gap", .address = 0x13 },
        NesRamProbeAddress{ .label = "nt1_pipe0_gap", .address = 0x14 },
        NesRamProbeAddress{ .label = "nt1_pipe1_gap", .address = 0x15 },
    };

    const Config::NesFlappyParatroopa config = makeFlappyProbeConfig(romPath);
    NesRamProbeStepper stepper(
        Scenario::EnumType::NesFlappyParatroopa,
        ScenarioConfig{ config },
        addresses,
        kFrameDeltaSeconds);
    EXPECT_TRUE(stepper.isRuntimeReady()) << stepper.getLastError();

    int waitingStateStableFrames = 0;
    for (int i = 0; i < 1600 && waitingStateStableFrames < 4; ++i) {
        uint8_t controllerMask = 0u;
        const SmolnesRuntime::MemorySnapshot* snapshot = stepper.getLastMemorySnapshot();
        if (snapshot != nullptr) {
            const uint8_t gameState = snapshot->cpuRam[kGameStateAddr];
            if (gameState == 0u || gameState == 7u) {
                controllerMask = SMOLNES_RUNTIME_BUTTON_START;
            }
        }

        const NesRamProbeFrame frame = stepper.step(controllerMask);
        if (!frame.cpuRamValues.empty() && frame.cpuRamValues.front() == 1u) {
            waitingStateStableFrames++;
        }
        else {
            waitingStateStableFrames = 0;
        }
    }
    EXPECT_GE(waitingStateStableFrames, 4) << "Failed to synchronize probe start on waiting state.";

    NesRamProbeTrace trace;
    trace.cpuAddresses = addresses;
    const auto script = buildScriptedControllerSequence();
    trace.frames.reserve(script.size());
    for (uint8_t controllerMask : script) {
        trace.frames.push_back(stepper.step(controllerMask));
    }
    return trace;
}

size_t findAddressIndex(const NesRamProbeTrace& trace, const std::string& label)
{
    const auto it = std::find_if(
        trace.cpuAddresses.begin(),
        trace.cpuAddresses.end(),
        [&label](const NesRamProbeAddress& address) { return address.label == label; });
    return it == trace.cpuAddresses.end() ? std::numeric_limits<size_t>::max()
                                          : static_cast<size_t>(it - trace.cpuAddresses.begin());
}

struct TraceSummary {
    bool sawNonZeroState = false;
    bool sawPlaying = false;
    bool sawTerminal = false;
    bool sawScrollAdvance = false;
    bool sawBirdMovement = false;
    uint64_t firstPlayingFrame = std::numeric_limits<uint64_t>::max();
    uint64_t firstTerminalFrame = std::numeric_limits<uint64_t>::max();
};

TraceSummary summarizeTrace(const NesRamProbeTrace& trace)
{
    TraceSummary summary;

    const size_t gameStateIndex = findAddressIndex(trace, "game_state");
    const size_t scrollXIndex = findAddressIndex(trace, "scroll_x");
    const size_t scrollNtIndex = findAddressIndex(trace, "scroll_nt");
    const size_t birdYIndex = findAddressIndex(trace, "bird_y");

    EXPECT_NE(gameStateIndex, std::numeric_limits<size_t>::max());
    EXPECT_NE(scrollXIndex, std::numeric_limits<size_t>::max());
    EXPECT_NE(scrollNtIndex, std::numeric_limits<size_t>::max());
    EXPECT_NE(birdYIndex, std::numeric_limits<size_t>::max());
    if (gameStateIndex == std::numeric_limits<size_t>::max()
        || scrollXIndex == std::numeric_limits<size_t>::max()
        || scrollNtIndex == std::numeric_limits<size_t>::max()
        || birdYIndex == std::numeric_limits<size_t>::max()) {
        return summary;
    }

    uint8_t previousScrollX = 0u;
    uint8_t previousScrollNt = 0u;
    uint8_t previousBirdY = 0u;
    bool hasPrevious = false;

    for (const NesRamProbeFrame& frame : trace.frames) {
        if (frame.cpuRamValues.size() <= gameStateIndex || frame.cpuRamValues.size() <= scrollXIndex
            || frame.cpuRamValues.size() <= scrollNtIndex
            || frame.cpuRamValues.size() <= birdYIndex) {
            continue;
        }

        const uint8_t gameState = frame.cpuRamValues[gameStateIndex];
        const uint8_t scrollX = frame.cpuRamValues[scrollXIndex];
        const uint8_t scrollNt = frame.cpuRamValues[scrollNtIndex];
        const uint8_t birdY = frame.cpuRamValues[birdYIndex];

        if (gameState != 0u) {
            summary.sawNonZeroState = true;
        }

        if (gameState == 2u) {
            summary.sawPlaying = true;
            if (summary.firstPlayingFrame == std::numeric_limits<uint64_t>::max()) {
                summary.firstPlayingFrame = frame.frame;
            }
        }

        if (gameState >= 3u && gameState <= 7u) {
            summary.sawTerminal = true;
            if (summary.firstTerminalFrame == std::numeric_limits<uint64_t>::max()) {
                summary.firstTerminalFrame = frame.frame;
            }
        }

        if (hasPrevious) {
            if (scrollX != previousScrollX || scrollNt != previousScrollNt) {
                summary.sawScrollAdvance = true;
            }

            if (birdY != previousBirdY) {
                summary.sawBirdMovement = true;
            }
        }

        previousScrollX = scrollX;
        previousScrollNt = scrollNt;
        previousBirdY = birdY;
        hasPrevious = true;
    }

    return summary;
}

} // namespace

TEST(NesRamProbeTest, DISABLED_ProbeCaptureIsDeterministicAndWritesCsvTrace)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    const NesRamProbeTrace firstTrace = runProbeTraceOnce(romPath.value());
    const NesRamProbeTrace secondTrace = runProbeTraceOnce(romPath.value());

    ASSERT_EQ(firstTrace.cpuAddresses.size(), secondTrace.cpuAddresses.size());
    ASSERT_EQ(firstTrace.frames.size(), secondTrace.frames.size());
    ASSERT_FALSE(firstTrace.frames.empty());

    for (size_t addressIndex = 0; addressIndex < firstTrace.cpuAddresses.size(); ++addressIndex) {
        EXPECT_EQ(
            firstTrace.cpuAddresses[addressIndex].label,
            secondTrace.cpuAddresses[addressIndex].label);
        EXPECT_EQ(
            firstTrace.cpuAddresses[addressIndex].address,
            secondTrace.cpuAddresses[addressIndex].address);
    }

    for (size_t frameIndex = 0; frameIndex < firstTrace.frames.size(); ++frameIndex) {
        const NesRamProbeFrame& first = firstTrace.frames[frameIndex];
        const NesRamProbeFrame& second = secondTrace.frames[frameIndex];

        EXPECT_EQ(first.frame, second.frame);
        EXPECT_EQ(first.controllerMask, second.controllerMask);
    }

    const TraceSummary firstSummary = summarizeTrace(firstTrace);
    const TraceSummary secondSummary = summarizeTrace(secondTrace);
    EXPECT_TRUE(firstSummary.sawNonZeroState);
    EXPECT_TRUE(firstSummary.sawPlaying);
    EXPECT_TRUE(firstSummary.sawTerminal);
    EXPECT_TRUE(firstSummary.sawScrollAdvance);
    EXPECT_TRUE(firstSummary.sawBirdMovement);
    EXPECT_TRUE(secondSummary.sawNonZeroState);
    EXPECT_TRUE(secondSummary.sawPlaying);
    EXPECT_TRUE(secondSummary.sawTerminal);
    EXPECT_TRUE(secondSummary.sawScrollAdvance);
    EXPECT_TRUE(secondSummary.sawBirdMovement);
    ASSERT_NE(firstSummary.firstPlayingFrame, std::numeric_limits<uint64_t>::max());
    ASSERT_NE(secondSummary.firstPlayingFrame, std::numeric_limits<uint64_t>::max());
    ASSERT_NE(firstSummary.firstTerminalFrame, std::numeric_limits<uint64_t>::max());
    ASSERT_NE(secondSummary.firstTerminalFrame, std::numeric_limits<uint64_t>::max());

    const uint64_t firstPlayingFrame = firstSummary.firstPlayingFrame;
    const uint64_t secondPlayingFrame = secondSummary.firstPlayingFrame;
    const uint64_t firstTerminalFrame = firstSummary.firstTerminalFrame;
    const uint64_t secondTerminalFrame = secondSummary.firstTerminalFrame;
    EXPECT_LE(
        std::max(firstPlayingFrame, secondPlayingFrame)
            - std::min(firstPlayingFrame, secondPlayingFrame),
        8u);
    EXPECT_LE(
        std::max(firstTerminalFrame, secondTerminalFrame)
            - std::min(firstTerminalFrame, secondTerminalFrame),
        24u);

    const std::filesystem::path tracePath =
        std::filesystem::path(::testing::TempDir()) / "nes_probe_trace.csv";
    EXPECT_TRUE(firstTrace.writeCsv(tracePath));
    EXPECT_TRUE(std::filesystem::exists(tracePath));
    EXPECT_GT(std::filesystem::file_size(tracePath), 0u);
}

TEST(NesRamProbeTest, ManualStep_BirdStartAndFlapSequence_PrintsTrace)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    const Config::NesFlappyParatroopa config = makeFlappyProbeConfig(romPath.value());
    FlappyParatroopaProbeStepper stepper{ config, kFrameDeltaSeconds };
    ASSERT_TRUE(stepper.isRuntimeReady()) << stepper.getLastError();

    bool startPressed = false;
    std::optional<FlappyParatroopaGameState> state;
    for (size_t frameIndex = 0; frameIndex < 200; ++frameIndex) {
        uint8_t controllerMask = 0u;
        if (!startPressed && state.has_value() && state->birdX > 5u) {
            controllerMask = SMOLNES_RUNTIME_BUTTON_START;
            startPressed = true;
        }
        else if (frameIndex % 1 == 0) {
            controllerMask = SMOLNES_RUNTIME_BUTTON_A;
        }

        state = stepper.step(controllerMask);
        ASSERT_TRUE(state.has_value());

        std::cout << "frameIndex: " << frameIndex
                  << ", controllerMask: " << static_cast<uint32_t>(stepper.getControllerMask())
                  << ", birdX: " << static_cast<uint32_t>(state->birdX)
                  << ", birdY: " << static_cast<uint32_t>(state->birdY)
                  << ", birdVelHi: " << static_cast<uint32_t>(state->birdVelocityHigh)
                  << ", scrollX: " << static_cast<uint32_t>(state->scrollX)
                  << ", scrollNt: " << static_cast<uint32_t>(state->scrollNt)
                  << ", score: " << static_cast<uint32_t>(state->scoreHundreds)
                  << static_cast<uint32_t>(state->scoreTens)
                  << static_cast<uint32_t>(state->scoreOnes)
                  << ", nt0Pipe0Gap: " << static_cast<uint32_t>(state->nt0Pipe0Gap)
                  << ", nt0Pipe1Gap: " << static_cast<uint32_t>(state->nt0Pipe1Gap)
                  << ", nt1Pipe0Gap: " << static_cast<uint32_t>(state->nt1Pipe0Gap)
                  << ", nt1Pipe1Gap: " << static_cast<uint32_t>(state->nt1Pipe1Gap)
                  << ", gamePhase: " << toString(state->gamePhase) << '\n';
    }

    EXPECT_TRUE(startPressed) << "Expected a Start press after the first bird_x > 0.";
}
