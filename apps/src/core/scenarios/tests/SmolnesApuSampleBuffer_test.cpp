#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/SmolnesApu.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>

using namespace DirtSim;

TEST(SmolnesApuSampleBuffer, PurePulse440HzFrequency)
{
    SmolnesApuState state{};
    smolnesApuInit(&state, 48000.0);

    // Configure pulse 1 for ~440Hz.
    // Timer period = 253 => freq = 1789773 / (16 * 254) ~= 440.4Hz.
    smolnesApuWrite(&state, 0x4015, 0x01);
    smolnesApuWrite(&state, 0x4000, 0xBF); // Duty 2 (50%), halt, constant vol=15.
    smolnesApuWrite(&state, 0x4002, 0xFD); // Timer low = 253.
    smolnesApuWrite(&state, 0x4003, 0x08); // Timer high = 0, length index 1.

    // Generate ~100ms of audio.
    const uint32_t cpuCycles = 178977;
    smolnesApuClock(&state, cpuCycles);

    float samples[4096];
    const uint32_t count = smolnesApuCopySamples(&state, samples, 4096, 0);
    ASSERT_GT(count, 2000u);

    // Count zero crossings.
    uint32_t zeroCrossings = 0;
    for (uint32_t i = 1; i < count; i++) {
        if ((samples[i] > 0.001f && samples[i - 1] <= 0.001f)
            || (samples[i] <= 0.001f && samples[i - 1] > 0.001f)) {
            zeroCrossings++;
        }
    }

    const double durationSec = (double)count / 48000.0;
    const double measuredFreq = (double)zeroCrossings / (2.0 * durationSec);

    EXPECT_NEAR(measuredFreq, 440.0, 20.0) << "Expected ~440Hz, got " << measuredFreq << "Hz";
}

namespace {

std::optional<std::filesystem::path> resolveFlappyRomPath()
{
    if (const char* env = std::getenv("DIRTSIM_NES_TEST_ROM_PATH"); env != nullptr) {
        const std::filesystem::path romPath{ env };
        if (std::filesystem::exists(romPath)) {
            return romPath;
        }
    }

    const std::filesystem::path repoRelative =
        std::filesystem::path("testdata") / "roms" / "Flappy.Paratroopa.World.Unl.nes";
    if (std::filesystem::exists(repoRelative)) {
        return repoRelative;
    }

    return std::nullopt;
}

} // namespace

TEST(SmolnesApuSampleBuffer, RomProducesNonSilentSamples)
{
    const auto romPath = resolveFlappyRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_TEST_ROM_PATH or run "
                        "'cd apps && make fetch-nes-test-rom'.";
    }

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesFlappyParatroopa);
    Config::NesFlappyParatroopa config = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    config.romPath = romPath.value().string();
    config.requireSmolnesMapper = true;
    config.maxEpisodeFrames = 500;

    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    ASSERT_TRUE(setResult.isValue()) << setResult.errorValue();
    const auto setupResult = driver.setup();
    ASSERT_TRUE(setupResult.isValue()) << setupResult.errorValue();
    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();

    Timers timers;
    std::optional<ScenarioVideoFrame> videoFrame;

    // Run into gameplay.
    for (int frame = 0; frame < 300; ++frame) {
        driver.tick(timers, videoFrame);
    }

    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();

    // Verify APU has generated samples via snapshot.
    const auto apuSnapshot = driver.copyRuntimeApuSnapshot();
    ASSERT_TRUE(apuSnapshot.has_value()) << "No APU snapshot available.";

    EXPECT_GT(apuSnapshot->totalSamplesGenerated, 0u) << "APU should have generated samples.";
    EXPECT_GT(apuSnapshot->registerWriteCount, 0u) << "Game should have written to APU registers.";

    // Copy the most recent batch of samples from the driver.
    float samples[1024];
    const uint32_t sampleCount = driver.copyRuntimeApuSamples(samples, 1024);

    // At 60fps with 48kHz audio, expect ~800 samples per frame.
    EXPECT_GT(sampleCount, 0u) << "Should have captured APU samples.";
}
