#include "core/scenarios/nes/NesTileTokenizerBootstrapper.h"

#include "core/Timers.h"
#include "core/scenarios/nes/NesGameAdapter.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/NesTileTokenizer.h"
#include "core/scenarios/nes/NesTileVocabularyBuilder.h"

namespace DirtSim {

Result<std::shared_ptr<NesTileTokenizer>, std::string> NesTileTokenizerBootstrapper::build(
    Scenario::EnumType scenarioId, const std::optional<ScenarioConfig>& scenarioConfigOverride)
{
    return build(scenarioId, scenarioConfigOverride, Config{});
}

Result<std::shared_ptr<NesTileTokenizer>, std::string> NesTileTokenizerBootstrapper::build(
    Scenario::EnumType scenarioId,
    const std::optional<ScenarioConfig>& scenarioConfigOverride,
    Config config)
{
    const NesGameAdapterRegistry adapterRegistry = NesGameAdapterRegistry::createDefault();
    return build(scenarioId, scenarioConfigOverride, adapterRegistry, config);
}

Result<std::shared_ptr<NesTileTokenizer>, std::string> NesTileTokenizerBootstrapper::build(
    Scenario::EnumType scenarioId,
    const std::optional<ScenarioConfig>& scenarioConfigOverride,
    const NesGameAdapterRegistry& adapterRegistry)
{
    return build(scenarioId, scenarioConfigOverride, adapterRegistry, Config{});
}

Result<std::shared_ptr<NesTileTokenizer>, std::string> NesTileTokenizerBootstrapper::build(
    Scenario::EnumType scenarioId,
    const std::optional<ScenarioConfig>& scenarioConfigOverride,
    const NesGameAdapterRegistry& adapterRegistry,
    Config config)
{
    if (config.bootstrapFrames <= 0) {
        return Result<std::shared_ptr<NesTileTokenizer>, std::string>::error(
            "NesTileTokenizerBootstrapper: bootstrapFrames must be positive");
    }

    NesSmolnesScenarioDriver driver(scenarioId);
    if (scenarioConfigOverride.has_value()) {
        const auto configResult = driver.setConfig(scenarioConfigOverride.value());
        if (configResult.isError()) {
            return Result<std::shared_ptr<NesTileTokenizer>, std::string>::error(
                "NesTileTokenizerBootstrapper: NES tile tokenizer config rejected: "
                + configResult.errorValue());
        }
    }

    const auto setupResult = driver.setup();
    if (setupResult.isError()) {
        return Result<std::shared_ptr<NesTileTokenizer>, std::string>::error(
            "NesTileTokenizerBootstrapper: NES tile tokenizer runtime setup failed: "
            + setupResult.errorValue());
    }

    auto adapter = adapterRegistry.createAdapter(scenarioId);
    if (!adapter) {
        return Result<std::shared_ptr<NesTileTokenizer>, std::string>::error(
            "NesTileTokenizerBootstrapper: Missing NES game adapter for tile tokenizer bootstrap");
    }
    adapter->reset(driver.getRuntimeResolvedRomId());

    NesTileVocabularyBuilder builder;
    Timers timers;
    std::optional<uint8_t> lastGameState = std::nullopt;

    for (int frameIndex = 0; frameIndex < config.bootstrapFrames; ++frameIndex) {
        if (const auto snapshot = driver.copyRuntimePpuSnapshot(); snapshot.has_value()) {
            builder.addSnapshot(snapshot.value());
        }

        const NesGameAdapterControllerOutput controllerOutput = adapter->resolveControllerMask(
            NesGameAdapterControllerInput{ .inferredControllerMask = 0,
                                           .lastGameState = lastGameState });
        auto stepResult = driver.step(timers, controllerOutput.resolvedControllerMask);
        if (!stepResult.runtimeHealthy || !stepResult.runtimeRunning) {
            if (builder.getSampledTileCount() == 0) {
                return Result<std::shared_ptr<NesTileTokenizer>, std::string>::error(
                    "NesTileTokenizerBootstrapper: NES tile tokenizer runtime stopped before "
                    "samples: "
                    + stepResult.lastError);
            }
            break;
        }

        if (const auto snapshot = driver.copyRuntimePpuSnapshot(); snapshot.has_value()) {
            builder.addSnapshot(snapshot.value());
        }

        if (stepResult.advancedFrames == 0) {
            continue;
        }

        const NesGameAdapterFrameInput frameInput{
            .advancedFrames = stepResult.advancedFrames,
            .controllerMask = controllerOutput.resolvedControllerMask,
            .paletteFrame =
                stepResult.paletteFrame.has_value() ? &stepResult.paletteFrame.value() : nullptr,
            .memorySnapshot = std::move(stepResult.memorySnapshot),
        };
        const NesGameAdapterFrameOutput frameOutput = adapter->evaluateFrame(frameInput);
        if (frameOutput.gameState.has_value()) {
            lastGameState = frameOutput.gameState;
        }
        if (frameOutput.done) {
            break;
        }
    }

    auto tokenizer = std::make_shared<NesTileTokenizer>();
    const auto buildResult = builder.buildFrozenTokenizer(*tokenizer);
    if (buildResult.isError()) {
        return Result<std::shared_ptr<NesTileTokenizer>, std::string>::error(
            "NesTileTokenizerBootstrapper: NES tile tokenizer bootstrap failed: "
            + buildResult.errorValue());
    }

    return Result<std::shared_ptr<NesTileTokenizer>, std::string>::okay(std::move(tokenizer));
}

} // namespace DirtSim
