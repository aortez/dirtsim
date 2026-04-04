#include "server/search/SmbSearchHarness.h"

#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesGameAdapter.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"

#include <memory>
#include <utility>

namespace DirtSim::Server::SearchSupport {

namespace {

struct PreparedRuntime {
    std::unique_ptr<NesGameAdapter> gameAdapter;
    std::unique_ptr<NesSmolnesScenarioDriver> driver;
    Timers timers;
};

Result<PreparedRuntime, std::string> prepareRuntime()
{
    PreparedRuntime prepared{};
    prepared.gameAdapter = NesGameAdapterRegistry::createDefault().createAdapter(
        Scenario::EnumType::NesSuperMarioBros);
    prepared.driver =
        std::make_unique<NesSmolnesScenarioDriver>(Scenario::EnumType::NesSuperMarioBros);

    if (!prepared.gameAdapter || !prepared.driver) {
        return Result<PreparedRuntime, std::string>::error("Failed to allocate SMB search runtime");
    }

    prepared.driver->setLiveServerPacingEnabled(false);
    const ScenarioConfig config = makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros);
    const auto configResult = prepared.driver->setConfig(config);
    if (configResult.isError()) {
        return Result<PreparedRuntime, std::string>::error(configResult.errorValue());
    }

    const auto setupResult = prepared.driver->setup();
    if (setupResult.isError()) {
        return Result<PreparedRuntime, std::string>::error(setupResult.errorValue());
    }

    prepared.gameAdapter->reset(prepared.driver->getRuntimeResolvedRomId());
    return Result<PreparedRuntime, std::string>::okay(std::move(prepared));
}

bool isGameplayFrame(
    const std::optional<uint8_t>& gameState, const NesGameAdapterControllerOutput& controllerOutput)
{
    return gameState.value_or(0u) == 1u
        && controllerOutput.source != NesGameAdapterControllerSource::ScriptedSetup;
}

uint64_t getFixtureTargetGameplayFrames(SmbSearchRootFixtureId fixtureId)
{
    switch (fixtureId) {
        case SmbSearchRootFixtureId::FlatGroundSanity:
            return 40u;
        case SmbSearchRootFixtureId::FirstGoomba:
            return 95u;
        case SmbSearchRootFixtureId::FirstGap:
            return 240u;
    }

    return 40u;
}

Result<SmbSearchReplayResult, std::string> replayFramesInternal(
    PreparedRuntime& prepared,
    const SmolnesRuntime::Savestate* rootSavestate,
    const SmbSearchEvaluatorSummary& rootSummary,
    const std::vector<PlayerControlFrame>& frames)
{
    if (rootSavestate != nullptr && !prepared.driver->loadRuntimeSavestate(*rootSavestate, 2000u)) {
        const std::string runtimeLastError = prepared.driver->getRuntimeLastError();
        return Result<SmbSearchReplayResult, std::string>::error(
            runtimeLastError.empty() ? "Failed to load SMB root savestate" : runtimeLastError);
    }

    NesSuperMarioBrosEvaluator evaluator;
    if (rootSummary.gameState.value_or(0u) == 1u) {
        evaluator.restoreProgress(
            decodeSmbStageIndex(rootSummary.bestFrontier),
            decodeSmbAbsoluteX(rootSummary.bestFrontier),
            rootSummary.distanceRewardTotal,
            rootSummary.levelClearRewardTotal,
            rootSummary.gameplayFrames,
            rootSummary.gameplayFramesSinceProgress);
    }

    NesSuperMarioBrosRamExtractor extractor;
    SmbSearchReplayResult replayResult{
        .evaluatorSummary = rootSummary,
    };
    std::optional<uint8_t> lastGameState = rootSummary.gameState;

    for (const PlayerControlFrame& frame : frames) {
        const auto stepResult =
            prepared.driver->step(prepared.timers, playerControlFrameToNesMask(frame));
        if (!stepResult.runtimeHealthy || !stepResult.runtimeRunning) {
            return Result<SmbSearchReplayResult, std::string>::error(
                stepResult.lastError.empty() ? "NES runtime stopped during replay"
                                             : stepResult.lastError);
        }
        if (stepResult.advancedFrames == 0) {
            continue;
        }
        if (!stepResult.memorySnapshot.has_value()) {
            return Result<SmbSearchReplayResult, std::string>::error(
                "NES replay step did not provide a memory snapshot");
        }

        const NesSuperMarioBrosState state =
            extractor.extract(stepResult.memorySnapshot.value(), true);
        lastGameState = state.phase == SmbPhase::Gameplay ? std::optional<uint8_t>(1u)
                                                          : std::optional<uint8_t>(0u);
        const auto evaluation = evaluator.evaluate(
            NesSuperMarioBrosEvaluatorInput{
                .advancedFrames = stepResult.advancedFrames,
                .state = state,
            });
        replayResult.evaluatorSummary =
            buildSmbSearchEvaluatorSummary(evaluation.snapshot, lastGameState);
        replayResult.memorySnapshot = stepResult.memorySnapshot;
        replayResult.scenarioVideoFrame = stepResult.scenarioVideoFrame;
        if (evaluation.done) {
            break;
        }
    }

    replayResult.memorySnapshot = prepared.driver->copyRuntimeMemorySnapshot();
    replayResult.savestate = prepared.driver->copyRuntimeSavestate();
    replayResult.scenarioVideoFrame = prepared.driver->copyRuntimeFrameSnapshot();
    if (!replayResult.savestate.has_value()) {
        return Result<SmbSearchReplayResult, std::string>::error(
            "Failed to capture SMB savestate after replay");
    }

    return Result<SmbSearchReplayResult, std::string>::okay(std::move(replayResult));
}

} // namespace

Result<SmbSearchRootFixture, std::string> SmbSearchHarness::captureGameplayRoot(
    uint64_t targetGameplayFrames, const std::string& name) const
{
    auto preparedResult = prepareRuntime();
    if (preparedResult.isError()) {
        return Result<SmbSearchRootFixture, std::string>::error(preparedResult.errorValue());
    }

    PreparedRuntime prepared = std::move(preparedResult).value();
    const PlayerControlFrame setupFrame =
        smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::RightRun);
    std::optional<uint8_t> lastGameState = std::nullopt;

    SmbSearchRootFixture fixture{};
    fixture.name = name;

    for (uint64_t stepIndex = 0; stepIndex < 2000u; ++stepIndex) {
        const NesGameAdapterControllerOutput controllerOutput =
            prepared.gameAdapter->resolveControllerMask(
                NesGameAdapterControllerInput{
                    .inferredControllerMask = playerControlFrameToNesMask(setupFrame),
                    .lastGameState = lastGameState,
                });
        const auto stepResult =
            prepared.driver->step(prepared.timers, controllerOutput.resolvedControllerMask);
        if (!stepResult.runtimeHealthy || !stepResult.runtimeRunning) {
            return Result<SmbSearchRootFixture, std::string>::error(
                stepResult.lastError.empty() ? "NES runtime stopped during fixture capture"
                                             : stepResult.lastError);
        }
        if (stepResult.advancedFrames == 0) {
            continue;
        }

        const NesGameAdapterFrameOutput evaluation = prepared.gameAdapter->evaluateFrame(
            NesGameAdapterFrameInput{
                .advancedFrames = stepResult.advancedFrames,
                .controllerMask = controllerOutput.resolvedControllerMask,
                .paletteFrame = stepResult.paletteFrame.has_value()
                    ? &stepResult.paletteFrame.value()
                    : nullptr,
                .memorySnapshot = stepResult.memorySnapshot,
            });
        if (evaluation.gameState.has_value()) {
            lastGameState = evaluation.gameState;
        }

        fixture.evaluatorSummary =
            buildSmbSearchEvaluatorSummary(evaluation.fitnessDetails, lastGameState);
        fixture.memorySnapshot = stepResult.memorySnapshot;
        fixture.scenarioVideoFrame = stepResult.scenarioVideoFrame;

        if (!isGameplayFrame(lastGameState, controllerOutput)) {
            continue;
        }
        if (fixture.evaluatorSummary.gameplayFrames < targetGameplayFrames) {
            continue;
        }

        const auto savestate = prepared.driver->copyRuntimeSavestate();
        if (!savestate.has_value()) {
            return Result<SmbSearchRootFixture, std::string>::error(
                "Failed to capture SMB savestate fixture");
        }
        fixture.savestate = savestate.value();
        const auto validationResult = replayFromRoot(
            fixture.savestate, fixture.evaluatorSummary, std::vector<PlayerControlFrame>{});
        if (validationResult.isError()) {
            continue;
        }
        return Result<SmbSearchRootFixture, std::string>::okay(std::move(fixture));
    }

    return Result<SmbSearchRootFixture, std::string>::error(
        "Timed out while capturing SMB root fixture");
}

Result<SmbSearchRootFixture, std::string> SmbSearchHarness::captureFixture(
    SmbSearchRootFixtureId fixtureId) const
{
    auto fixtureResult =
        captureGameplayRoot(getFixtureTargetGameplayFrames(fixtureId), toString(fixtureId));
    if (fixtureResult.isError()) {
        return fixtureResult;
    }

    fixtureResult.value().id = fixtureId;
    return fixtureResult;
}

Result<SmbSearchReplayResult, std::string> SmbSearchHarness::replayFromRoot(
    const SmolnesRuntime::Savestate& rootSavestate,
    const SmbSearchEvaluatorSummary& rootSummary,
    const std::vector<PlayerControlFrame>& frames) const
{
    auto preparedResult = prepareRuntime();
    if (preparedResult.isError()) {
        return Result<SmbSearchReplayResult, std::string>::error(preparedResult.errorValue());
    }

    PreparedRuntime prepared = std::move(preparedResult).value();
    const auto warmupStepResult = prepared.driver->step(prepared.timers, 0u);
    if (!warmupStepResult.runtimeHealthy || !warmupStepResult.runtimeRunning) {
        return Result<SmbSearchReplayResult, std::string>::error(
            warmupStepResult.lastError.empty() ? "NES runtime stopped during replay warmup"
                                               : warmupStepResult.lastError);
    }
    return replayFramesInternal(prepared, &rootSavestate, rootSummary, frames);
}

Result<SmbSearchReplayResult, std::string> SmbSearchHarness::replayFromRoot(
    const SmolnesRuntime::Savestate& rootSavestate,
    const SmbSearchEvaluatorSummary& rootSummary,
    const std::vector<SmbSearchLegalAction>& actions) const
{
    std::vector<PlayerControlFrame> frames;
    frames.reserve(actions.size());
    for (const SmbSearchLegalAction action : actions) {
        frames.push_back(smbSearchLegalActionToPlayerControlFrame(action));
    }

    return replayFromRoot(rootSavestate, rootSummary, frames);
}

std::string toString(SmbSearchRootFixtureId fixtureId)
{
    switch (fixtureId) {
        case SmbSearchRootFixtureId::FlatGroundSanity:
            return "FlatGroundSanity";
        case SmbSearchRootFixtureId::FirstGoomba:
            return "FirstGoomba";
        case SmbSearchRootFixtureId::FirstGap:
            return "FirstGap";
    }

    return "Unknown";
}

} // namespace DirtSim::Server::SearchSupport
