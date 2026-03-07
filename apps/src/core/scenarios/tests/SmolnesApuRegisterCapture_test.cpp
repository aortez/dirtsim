#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>

using namespace DirtSim;

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

TEST(SmolnesApuRegisterCapture, FlappyParatroopaProducesApuWrites)
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

    // Run ~300 frames past the title screen.
    for (int frame = 0; frame < 300; ++frame) {
        driver.tick(timers, videoFrame);
    }

    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();

    const auto apuSnapshot = driver.copyRuntimeApuSnapshot();
    ASSERT_TRUE(apuSnapshot.has_value()) << "No APU snapshot available.";

    EXPECT_GT(apuSnapshot->registerWriteCount, 0u) << "Game should have written to APU registers.";

    // At least one channel should be enabled.
    const bool anyChannelEnabled = apuSnapshot->pulse1Enabled || apuSnapshot->pulse2Enabled
        || apuSnapshot->triangleEnabled || apuSnapshot->noiseEnabled;

    EXPECT_TRUE(anyChannelEnabled) << "At least one APU channel should be enabled after gameplay.";

    EXPECT_GT(apuSnapshot->totalSamplesGenerated, 0u)
        << "APU should have generated samples during gameplay.";
}
