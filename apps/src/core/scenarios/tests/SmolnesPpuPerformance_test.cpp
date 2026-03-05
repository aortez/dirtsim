#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesPaletteFrame.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <string>

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

TEST(SmolnesPpuPerformance, FlappyParatroopa1000Frames)
{
    const auto romPath = resolveFlappyRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Set DIRTSIM_NES_TEST_ROM_PATH or run "
                        "'cd apps && make fetch-nes-test-rom'.";
    }

    constexpr int kFrameCount = 1000;

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesFlappyParatroopa);
    Config::NesFlappyParatroopa config = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    config.romPath = romPath.value().string();
    config.requireSmolnesMapper = true;
    config.maxEpisodeFrames = kFrameCount + 100;

    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    ASSERT_TRUE(setResult.isValue()) << setResult.errorValue();
    const auto setupResult = driver.setup();
    ASSERT_TRUE(setupResult.isValue()) << setupResult.errorValue();
    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();
    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();

    Timers timers;
    std::optional<ScenarioVideoFrame> videoFrame;
    std::optional<NesPaletteFrame> paletteFrame100;
    std::optional<NesPaletteFrame> paletteFrame500;

    const auto wallStart = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kFrameCount; ++frame) {
        driver.tick(timers, videoFrame);
        if (frame == 100) {
            paletteFrame100 = driver.copyRuntimePaletteFrame();
        }
        else if (frame == 500) {
            paletteFrame500 = driver.copyRuntimePaletteFrame();
        }
    }
    const auto wallEnd = std::chrono::steady_clock::now();
    const double wallMs = std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();

    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();
    EXPECT_EQ(driver.getRuntimeRenderedFrameCount(), static_cast<uint64_t>(kFrameCount));

    // Collect profiling results from Timers (populated by driver.tick).
    const double frameExecMs = timers.getAccumulatedTime("nes_runtime_thread_frame_execution");
    const double cpuStepMs = timers.getAccumulatedTime("nes_runtime_thread_cpu_step");
    const double ppuStepMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_step");
    const double ppuVisibleMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_visible_pixels");
    const double ppuSpriteMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_sprite_eval");
    const double ppuPrefetchMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_prefetch");
    const double ppuOtherMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_other");
    const double frameSubmitMs = timers.getAccumulatedTime("nes_runtime_thread_frame_submit");
    const double eventPollMs = timers.getAccumulatedTime("nes_runtime_thread_event_poll");
    const double presentMs = timers.getAccumulatedTime("nes_runtime_thread_present");
    const double memCopyMs = timers.getAccumulatedTime("nes_runtime_memory_snapshot_copy");

    const double ppuPhaseTotalMs = ppuVisibleMs + ppuSpriteMs + ppuPrefetchMs + ppuOtherMs;

    auto pct = [](double part, double whole) -> double {
        return whole > 0.0 ? (part / whole) * 100.0 : 0.0;
    };

    fprintf(
        stderr,
        "\n"
        "=== SmolNES PPU Performance: %d frames ===\n"
        "\n"
        "Wall clock:              %8.1f ms  (%5.2f ms/frame, %.1f fps)\n"
        "\n"
        "Frame execution total:   %8.1f ms  (%5.2f ms/frame)\n"
        "  CPU step:              %8.1f ms  (%5.1f%%)\n"
        "  PPU step:              %8.1f ms  (%5.1f%%)\n"
        "  Frame submit:          %8.1f ms  (%5.1f%%)\n"
        "  Event poll:            %8.1f ms  (%5.1f%%)\n"
        "\n"
        "PPU phase breakdown:     %8.1f ms\n"
        "  Visible pixels:        %8.1f ms  (%5.1f%%)\n"
        "  Sprite eval:           %8.1f ms  (%5.1f%%)\n"
        "  Prefetch:              %8.1f ms  (%5.1f%%)\n"
        "  Other:                 %8.1f ms  (%5.1f%%)\n"
        "\n"
        "Other:\n"
        "  Present (sync+copy):   %8.1f ms\n"
        "  Memory snapshot copy:  %8.1f ms\n"
        "\n",
        kFrameCount,
        wallMs,
        wallMs / kFrameCount,
        kFrameCount / (wallMs / 1000.0),
        frameExecMs,
        frameExecMs / kFrameCount,
        cpuStepMs,
        pct(cpuStepMs, frameExecMs),
        ppuStepMs,
        pct(ppuStepMs, frameExecMs),
        frameSubmitMs,
        pct(frameSubmitMs, frameExecMs),
        eventPollMs,
        pct(eventPollMs, frameExecMs),
        ppuPhaseTotalMs,
        ppuVisibleMs,
        pct(ppuVisibleMs, ppuPhaseTotalMs),
        ppuSpriteMs,
        pct(ppuSpriteMs, ppuPhaseTotalMs),
        ppuPrefetchMs,
        pct(ppuPrefetchMs, ppuPhaseTotalMs),
        ppuOtherMs,
        pct(ppuOtherMs, ppuPhaseTotalMs),
        presentMs,
        memCopyMs);

    // Correctness: verify rendering produced non-trivial output.
    ASSERT_TRUE(paletteFrame100.has_value()) << "No palette frame at frame 100.";
    ASSERT_TRUE(paletteFrame500.has_value()) << "No palette frame at frame 500.";
    ASSERT_FALSE(paletteFrame100->indices.empty()) << "Palette frame 100 has no data.";
    ASSERT_FALSE(paletteFrame500->indices.empty()) << "Palette frame 500 has no data.";

    const bool frame100AllZero = std::all_of(
        paletteFrame100->indices.begin(), paletteFrame100->indices.end(), [](uint8_t v) {
            return v == 0;
        });
    const bool frame500AllZero = std::all_of(
        paletteFrame500->indices.begin(), paletteFrame500->indices.end(), [](uint8_t v) {
            return v == 0;
        });
    EXPECT_FALSE(frame100AllZero) << "Palette frame 100 is all-zero (rendering broken).";
    EXPECT_FALSE(frame500AllZero) << "Palette frame 500 is all-zero (rendering broken).";

    EXPECT_NE(paletteFrame100->indices, paletteFrame500->indices)
        << "Palette frames 100 and 500 are identical (game not progressing).";
}
