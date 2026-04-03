#include "NesTestRomPath.h"
#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesPaletteFrame.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <optional>
#include <string>

using namespace DirtSim;

namespace {

struct PpuPerfParam {
    Scenario::EnumType scenarioId;
    std::function<std::optional<std::filesystem::path>()> resolveRom;
    const char* label;
    bool detailedTiming = true;
    bool apuEnabled = true;
    bool pixelOutputEnabled = true;
    bool rgbaOutputEnabled = true;
};

std::string nameGenerator(const testing::TestParamInfo<PpuPerfParam>& info)
{
    return info.param.label;
}

} // namespace

class SmolnesPpuPerformance : public testing::TestWithParam<PpuPerfParam> {};

TEST_P(SmolnesPpuPerformance, Run1000Frames)
{
    const auto& param = GetParam();
    const auto romPath = param.resolveRom();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing for " << param.label << ".";
    }

    constexpr int kFrameCount = 1000;

    NesSmolnesScenarioDriver driver(param.scenarioId);
    ScenarioConfig config = makeDefaultConfig(param.scenarioId);
    std::visit(
        [&](auto& c) {
            if constexpr (requires {
                              c.romPath;
                              c.requireSmolnesMapper;
                              c.maxEpisodeFrames;
                          }) {
                c.romPath = romPath.value().string();
                c.requireSmolnesMapper = true;
                c.maxEpisodeFrames = kFrameCount + 100;
            }
        },
        config);

    const auto setResult = driver.setConfig(config);
    ASSERT_TRUE(setResult.isValue()) << setResult.errorValue();
    const auto setupResult = driver.setup();
    ASSERT_TRUE(setupResult.isValue()) << setupResult.errorValue();
    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();
    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();

    if (!param.apuEnabled) {
        driver.setApuEnabled(false);
    }
    if (!param.pixelOutputEnabled) {
        driver.setPixelOutputEnabled(false);
    }
    if (!param.rgbaOutputEnabled) {
        driver.setRgbaOutputEnabled(false);
    }
    driver.setDetailedTimingEnabled(param.detailedTiming);

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
    const auto profilingSnapshot = driver.copyRuntimeProfilingSnapshot();

    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();
    EXPECT_EQ(driver.getRuntimeRenderedFrameCount(), static_cast<uint64_t>(kFrameCount));

    // Collect profiling results from Timers (populated by driver.tick).
    const double frameExecMs = timers.getAccumulatedTime("nes_runtime_thread_frame_execution");
    const double cpuStepMs = timers.getAccumulatedTime("nes_runtime_thread_cpu_step");
    const double apuStepMs = timers.getAccumulatedTime("nes_runtime_thread_apu_step");
    const double ppuStepMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_step");
    const double ppuVisiblePixelsMs =
        timers.getAccumulatedTime("nes_runtime_thread_ppu_visible_pixels");
    const double ppuSpriteEvalMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_sprite_eval");
    const double ppuPostVisibleMs =
        timers.getAccumulatedTime("nes_runtime_thread_ppu_post_visible");
    const double ppuPrefetchMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_prefetch");
    const double ppuNonVisibleScanlinesMs =
        timers.getAccumulatedTime("nes_runtime_thread_ppu_non_visible_scanlines");
    const double ppuOtherMs = timers.getAccumulatedTime("nes_runtime_thread_ppu_other");
    const double frameSubmitMs = timers.getAccumulatedTime("nes_runtime_thread_frame_submit");
    const double presentMs = timers.getAccumulatedTime("nes_runtime_thread_present");
    const double memCopyMs = timers.getAccumulatedTime("nes_runtime_memory_snapshot_copy");

    auto clamp0 = [](double v) -> double { return v > 0.0 ? v : 0.0; };

    if (param.detailedTiming) {
        // Sampled times include per-sample clock_gettime overhead, so absolute
        // values are inflated. Use the ratios between phases to estimate their
        // share of frame execution time.
        const double sampledTotal = clamp0(cpuStepMs) + clamp0(apuStepMs) + clamp0(ppuStepMs);
        const double cpuPct = sampledTotal > 0.0 ? clamp0(cpuStepMs) / sampledTotal * 100.0 : 0.0;
        const double apuPct = sampledTotal > 0.0 ? clamp0(apuStepMs) / sampledTotal * 100.0 : 0.0;
        const double ppuPct = sampledTotal > 0.0 ? clamp0(ppuStepMs) / sampledTotal * 100.0 : 0.0;
        const double cpuEstMs = clamp0(frameExecMs) * cpuPct / 100.0;
        const double apuEstMs = clamp0(frameExecMs) * apuPct / 100.0;
        const double ppuEstMs = clamp0(frameExecMs) * ppuPct / 100.0;
        const double sampledPpuTotal = clamp0(ppuVisiblePixelsMs) + clamp0(ppuSpriteEvalMs)
            + clamp0(ppuPostVisibleMs) + clamp0(ppuPrefetchMs) + clamp0(ppuNonVisibleScanlinesMs)
            + clamp0(ppuOtherMs);
        const double ppuVisiblePixelsPct =
            sampledPpuTotal > 0.0 ? clamp0(ppuVisiblePixelsMs) / sampledPpuTotal * 100.0 : 0.0;
        const double ppuSpriteEvalPct =
            sampledPpuTotal > 0.0 ? clamp0(ppuSpriteEvalMs) / sampledPpuTotal * 100.0 : 0.0;
        const double ppuPostVisiblePct =
            sampledPpuTotal > 0.0 ? clamp0(ppuPostVisibleMs) / sampledPpuTotal * 100.0 : 0.0;
        const double ppuPrefetchPct =
            sampledPpuTotal > 0.0 ? clamp0(ppuPrefetchMs) / sampledPpuTotal * 100.0 : 0.0;
        const double ppuNonVisibleScanlinesPct = sampledPpuTotal > 0.0
            ? clamp0(ppuNonVisibleScanlinesMs) / sampledPpuTotal * 100.0
            : 0.0;
        const double ppuOtherPct =
            sampledPpuTotal > 0.0 ? clamp0(ppuOtherMs) / sampledPpuTotal * 100.0 : 0.0;
        const double ppuVisiblePixelsEstMs = ppuEstMs * ppuVisiblePixelsPct / 100.0;
        const double ppuSpriteEvalEstMs = ppuEstMs * ppuSpriteEvalPct / 100.0;
        const double ppuPostVisibleEstMs = ppuEstMs * ppuPostVisiblePct / 100.0;
        const double ppuPrefetchEstMs = ppuEstMs * ppuPrefetchPct / 100.0;
        const double ppuNonVisibleScanlinesEstMs = ppuEstMs * ppuNonVisibleScanlinesPct / 100.0;
        const double ppuOtherEstMs = ppuEstMs * ppuOtherPct / 100.0;
        const uint64_t bgOnlySpanCalls = profilingSnapshot.has_value()
            ? profilingSnapshot->runtimeThreadPpuVisibleBgOnlySpanCalls
            : 0;
        const uint64_t bgOnlySpanPixels = profilingSnapshot.has_value()
            ? profilingSnapshot->runtimeThreadPpuVisibleBgOnlySpanPixels
            : 0;
        const uint64_t bgOnlyScalarPixels = profilingSnapshot.has_value()
            ? profilingSnapshot->runtimeThreadPpuVisibleBgOnlyScalarPixels
            : 0;
        const uint64_t bgOnlyBatchedPixels = profilingSnapshot.has_value()
            ? profilingSnapshot->runtimeThreadPpuVisibleBgOnlyBatchedPixels
            : 0;
        const uint64_t bgOnlyBatchedCalls = profilingSnapshot.has_value()
            ? profilingSnapshot->runtimeThreadPpuVisibleBgOnlyBatchedCalls
            : 0;
        const double bgOnlyAvgSpanPixels = bgOnlySpanCalls > 0
            ? static_cast<double>(bgOnlySpanPixels) / static_cast<double>(bgOnlySpanCalls)
            : 0.0;
        const double bgOnlyScalarPixelsPct = bgOnlySpanPixels > 0
            ? static_cast<double>(bgOnlyScalarPixels) / static_cast<double>(bgOnlySpanPixels)
                * 100.0
            : 0.0;
        const double bgOnlyBatchedPixelsPct = bgOnlySpanPixels > 0
            ? static_cast<double>(bgOnlyBatchedPixels) / static_cast<double>(bgOnlySpanPixels)
                * 100.0
            : 0.0;
        const double bgOnlyAvgBatchesPerCall = bgOnlySpanCalls > 0
            ? static_cast<double>(bgOnlyBatchedCalls) / static_cast<double>(bgOnlySpanCalls)
            : 0.0;

        fprintf(
            stderr,
            "\n"
            "=== SmolNES Profiled [%s]: %d frames (sampled) ===\n"
            "\n"
            "Wall clock:              %8.1f ms  (%5.2f ms/frame, %.1f fps)\n"
            "\n"
            "Frame execution total:   %8.1f ms  (%5.2f ms/frame)\n"
            "  CPU step (est):        %8.1f ms  (%5.1f%%)\n"
            "  APU step (est):        %8.1f ms  (%5.1f%%)\n"
            "  PPU step (est):        %8.1f ms  (%5.1f%%)\n"
            "    Visible pixels:      %8.1f ms  (%5.1f%% of PPU)\n"
            "    Sprite eval:         %8.1f ms  (%5.1f%% of PPU)\n"
            "    Post-visible 256-319:%8.1f ms  (%5.1f%% of PPU)\n"
            "    Prefetch:            %8.1f ms  (%5.1f%% of PPU)\n"
            "    Non-visible scans:   %8.1f ms  (%5.1f%% of PPU)\n"
            "    Other:               %8.1f ms  (%5.1f%% of PPU)\n"
            "\n"
            "Background-only visible spans:\n"
            "  Span calls:            %8llu\n"
            "  Avg span length:       %8.2f pixels\n"
            "  Scalar pixels:         %8llu  (%5.1f%%)\n"
            "  Batched pixels:        %8llu  (%5.1f%%)\n"
            "  Avg 8px batches/call: %8.2f\n"
            "\n"
            "Outside frame execution:\n"
            "  Frame submit:          %8.1f ms\n"
            "  Present (sync+copy):   %8.1f ms\n"
            "  Memory snapshot copy:  %8.1f ms\n"
            "\n",
            param.label,
            kFrameCount,
            wallMs,
            wallMs / kFrameCount,
            kFrameCount / (wallMs / 1000.0),
            frameExecMs,
            frameExecMs / kFrameCount,
            cpuEstMs,
            cpuPct,
            apuEstMs,
            apuPct,
            ppuEstMs,
            ppuPct,
            ppuVisiblePixelsEstMs,
            ppuVisiblePixelsPct,
            ppuSpriteEvalEstMs,
            ppuSpriteEvalPct,
            ppuPostVisibleEstMs,
            ppuPostVisiblePct,
            ppuPrefetchEstMs,
            ppuPrefetchPct,
            ppuNonVisibleScanlinesEstMs,
            ppuNonVisibleScanlinesPct,
            ppuOtherEstMs,
            ppuOtherPct,
            static_cast<unsigned long long>(bgOnlySpanCalls),
            bgOnlyAvgSpanPixels,
            static_cast<unsigned long long>(bgOnlyScalarPixels),
            bgOnlyScalarPixelsPct,
            static_cast<unsigned long long>(bgOnlyBatchedPixels),
            bgOnlyBatchedPixelsPct,
            bgOnlyAvgBatchesPerCall,
            frameSubmitMs,
            presentMs,
            memCopyMs);
    }
    else {
        fprintf(
            stderr,
            "\n"
            "=== SmolNES Throughput [%s]: %d frames ===\n"
            "\n"
            "Wall clock:              %8.1f ms  (%5.2f ms/frame, %.1f fps)\n"
            "Frame execution total:   %8.1f ms  (%5.2f ms/frame)\n"
            "\n"
            "Outside frame execution:\n"
            "  Frame submit:          %8.1f ms\n"
            "  Present (sync+copy):   %8.1f ms\n"
            "  Memory snapshot copy:  %8.1f ms\n"
            "\n",
            param.label,
            kFrameCount,
            wallMs,
            wallMs / kFrameCount,
            kFrameCount / (wallMs / 1000.0),
            frameExecMs,
            frameExecMs / kFrameCount,
            frameSubmitMs,
            presentMs,
            memCopyMs);
    }

    if (param.pixelOutputEnabled) {
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
}

INSTANTIATE_TEST_SUITE_P(
    NesRoms,
    SmolnesPpuPerformance,
    testing::Values(
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesFlappyParatroopa,
            .resolveRom = Test::resolveFlappyRomPath,
            .label = "FlappyParatroopa_Profiled",
            .detailedTiming = true,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesFlappyParatroopa,
            .resolveRom = Test::resolveFlappyRomPath,
            .label = "FlappyParatroopa_Throughput",
            .detailedTiming = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesFlappyParatroopa,
            .resolveRom = Test::resolveFlappyRomPath,
            .label = "FlappyParatroopa_Throughput_NoApu",
            .detailedTiming = false,
            .apuEnabled = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesFlappyParatroopa,
            .resolveRom = Test::resolveFlappyRomPath,
            .label = "FlappyParatroopa_Throughput_NoApu_PaletteOnly",
            .detailedTiming = false,
            .apuEnabled = false,
            .rgbaOutputEnabled = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesFlappyParatroopa,
            .resolveRom = Test::resolveFlappyRomPath,
            .label = "FlappyParatroopa_Throughput_NoApu_NoPixels",
            .detailedTiming = false,
            .apuEnabled = false,
            .pixelOutputEnabled = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesSuperMarioBros,
            .resolveRom = Test::resolveSmbRomPath,
            .label = "SuperMarioBros_Profiled",
            .detailedTiming = true,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesSuperMarioBros,
            .resolveRom = Test::resolveSmbRomPath,
            .label = "SuperMarioBros_Profiled_NoApu_PaletteOnly",
            .detailedTiming = true,
            .apuEnabled = false,
            .rgbaOutputEnabled = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesSuperMarioBros,
            .resolveRom = Test::resolveSmbRomPath,
            .label = "SuperMarioBros_Profiled_NoApu_NoPixels",
            .detailedTiming = true,
            .apuEnabled = false,
            .pixelOutputEnabled = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesSuperMarioBros,
            .resolveRom = Test::resolveSmbRomPath,
            .label = "SuperMarioBros_Throughput",
            .detailedTiming = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesSuperMarioBros,
            .resolveRom = Test::resolveSmbRomPath,
            .label = "SuperMarioBros_Throughput_NoApu",
            .detailedTiming = false,
            .apuEnabled = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesSuperMarioBros,
            .resolveRom = Test::resolveSmbRomPath,
            .label = "SuperMarioBros_Throughput_NoApu_PaletteOnly",
            .detailedTiming = false,
            .apuEnabled = false,
            .rgbaOutputEnabled = false,
        },
        PpuPerfParam{
            .scenarioId = Scenario::EnumType::NesSuperMarioBros,
            .resolveRom = Test::resolveSmbRomPath,
            .label = "SuperMarioBros_Throughput_NoApu_NoPixels",
            .detailedTiming = false,
            .apuEnabled = false,
            .pixelOutputEnabled = false,
        }),
    nameGenerator);
