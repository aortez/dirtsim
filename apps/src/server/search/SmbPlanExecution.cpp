#include "SmbPlanExecution.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/UUID.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"
#include <spdlog/spdlog.h>

namespace DirtSim::Server::SearchSupport {

namespace {

std::unique_ptr<NesGameAdapter> createSmbGameAdapter()
{
    return NesGameAdapterRegistry::createDefault().createAdapter(
        Scenario::EnumType::NesSuperMarioBros);
}

bool isGameplayFrame(
    const std::optional<uint8_t>& gameState, const NesGameAdapterControllerOutput& controllerOutput)
{
    return gameState.value_or(0u) == 1u
        && controllerOutput.source != NesGameAdapterControllerSource::ScriptedSetup;
}

SmolnesRuntime::Savestate makeSavestate(const Api::SmbPlaybackRoot& playbackRoot)
{
    SmolnesRuntime::Savestate savestate{};
    savestate.frameId = playbackRoot.savestateFrameId;
    savestate.bytes.reserve(playbackRoot.savestateBytes.size());
    for (const uint8_t value : playbackRoot.savestateBytes) {
        savestate.bytes.push_back(static_cast<std::byte>(value));
    }
    return savestate;
}

} // namespace

Result<std::monostate, std::string> SmbPlanExecution::startHoldRight()
{
    mode_ = Mode::HoldRightSearch;
    plan_ = Api::Plan{};
    plan_.summary.id = UUID::generate();
    playbackFrameIndex_ = 0;
    return startCommon();
}

Result<std::monostate, std::string> SmbPlanExecution::startPlayback(const Api::Plan& plan)
{
    mode_ = Mode::PlanPlayback;
    plan_ = plan;
    playbackFrameIndex_ = 0;
    return startCommon();
}

Result<std::monostate, std::string> SmbPlanExecution::startCommon()
{
    driver_ = std::make_unique<NesSmolnesScenarioDriver>(Scenario::EnumType::NesSuperMarioBros);
    driver_->setLiveServerPacingEnabled(false);

    const ScenarioConfig scenarioConfig = makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros);
    const auto configResult = driver_->setConfig(scenarioConfig);
    if (configResult.isError()) {
        return Result<std::monostate, std::string>::error(configResult.errorValue());
    }

    const auto setupResult = driver_->setup();
    if (setupResult.isError()) {
        return Result<std::monostate, std::string>::error(setupResult.errorValue());
    }

    gameAdapter_ = createSmbGameAdapter();
    if (!gameAdapter_) {
        return Result<std::monostate, std::string>::error("Failed to create SMB game adapter");
    }

    gameAdapter_->reset(driver_->getRuntimeResolvedRomId());
    evaluator_.reset();
    worldData_ = WorldData{};
    worldData_.width = 256;
    worldData_.height = 240;
    scenarioVideoFrame_.reset();
    lastGameState_.reset();
    progress_ = Api::SearchProgress{};
    progressFrameOffset_ = 0;
    completed_ = false;
    paused_ = false;

    if (mode_ == Mode::PlanPlayback && plan_.smbPlaybackRoot.has_value()) {
        const auto warmupStepResult = driver_->step(timers_, 0u);
        if (!warmupStepResult.runtimeHealthy || !warmupStepResult.runtimeRunning) {
            return Result<std::monostate, std::string>::error(
                warmupStepResult.lastError.empty() ? "NES runtime stopped during playback warmup"
                                                   : warmupStepResult.lastError);
        }

        const Api::SmbPlaybackRoot& playbackRoot = plan_.smbPlaybackRoot.value();
        const SmolnesRuntime::Savestate savestate = makeSavestate(playbackRoot);
        if (!driver_->loadRuntimeSavestate(savestate, 2000u)) {
            const std::string runtimeLastError = driver_->getRuntimeLastError();
            return Result<std::monostate, std::string>::error(
                runtimeLastError.empty() ? "Failed to load SMB playback root savestate"
                                         : runtimeLastError);
        }

        evaluator_.restoreProgress(
            decodeSmbStageIndex(playbackRoot.bestFrontier),
            decodeSmbAbsoluteX(playbackRoot.bestFrontier),
            playbackRoot.distanceRewardTotal,
            playbackRoot.levelClearRewardTotal,
            playbackRoot.gameplayFrames,
            playbackRoot.gameplayFramesSinceProgress);
        progressFrameOffset_ = playbackRoot.gameplayFrames;
        lastGameState_ = playbackRoot.gameState;
        scenarioVideoFrame_ = driver_->copyRuntimeFrameSnapshot();
        if (scenarioVideoFrame_.has_value()) {
            worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
            worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
        }
        worldData_.timestep = static_cast<int32_t>(playbackRoot.gameplayFrames);
        updateProgress(
            SmbSearchEvaluatorSummary{
                .bestFrontier = playbackRoot.bestFrontier,
                .gameplayFrames = playbackRoot.gameplayFrames,
                .gameplayFramesSinceProgress = playbackRoot.gameplayFramesSinceProgress,
                .distanceRewardTotal = playbackRoot.distanceRewardTotal,
                .evaluationScore = playbackRoot.evaluationScore,
                .levelClearRewardTotal = playbackRoot.levelClearRewardTotal,
                .gameState = playbackRoot.gameState,
                .terminal = playbackRoot.terminal,
            });
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

SmbPlanExecutionTickResult SmbPlanExecution::tick()
{
    if (completed_) {
        return SmbPlanExecutionTickResult{ .completed = true };
    }

    if (paused_) {
        return {};
    }

    if (!driver_ || !gameAdapter_) {
        completed_ = true;
        return SmbPlanExecutionTickResult{
            .completed = true,
            .error = std::string("SMB execution not initialized"),
        };
    }

    if (mode_ == Mode::PlanPlayback && playbackFrameIndex_ >= plan_.frames.size()) {
        completed_ = true;
        return SmbPlanExecutionTickResult{ .completed = true };
    }

    const PlayerControlFrame playerFrame =
        mode_ == Mode::PlanPlayback ? plan_.frames[playbackFrameIndex_] : holdRightFrame();
    const uint8_t inferredControllerMask = playerControlFrameToNesMask(playerFrame);
    const NesGameAdapterControllerOutput controllerOutput = gameAdapter_->resolveControllerMask(
        NesGameAdapterControllerInput{
            .inferredControllerMask = inferredControllerMask,
            .lastGameState = lastGameState_,
        });

    const auto stepResult = driver_->step(timers_, controllerOutput.resolvedControllerMask);
    if (stepResult.scenarioVideoFrame.has_value()) {
        scenarioVideoFrame_ = stepResult.scenarioVideoFrame;
        worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
        worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
    }

    if (!stepResult.runtimeHealthy || !stepResult.runtimeRunning) {
        completed_ = true;
        return SmbPlanExecutionTickResult{
            .completed = true,
            .error = stepResult.lastError.empty()
                ? std::optional<std::string>("NES runtime stopped")
                : std::optional<std::string>(stepResult.lastError),
        };
    }

    if (stepResult.advancedFrames == 0) {
        return {};
    }

    worldData_.timestep += static_cast<int32_t>(stepResult.advancedFrames);

    if (!stepResult.memorySnapshot.has_value()) {
        completed_ = true;
        return SmbPlanExecutionTickResult{
            .completed = true,
            .error = std::string("NES playback step did not provide a memory snapshot"),
        };
    }

    const NesSuperMarioBrosState state = ramExtractor_.extract(
        stepResult.memorySnapshot.value(),
        controllerOutput.source != NesGameAdapterControllerSource::ScriptedSetup);
    lastGameState_ =
        state.phase == SmbPhase::Gameplay ? std::optional<uint8_t>(1u) : std::optional<uint8_t>(0u);
    const auto evaluation = evaluator_.evaluate(
        NesSuperMarioBrosEvaluatorInput{
            .advancedFrames = stepResult.advancedFrames,
            .state = state,
        });

    updateProgress(buildSmbSearchEvaluatorSummary(evaluation.snapshot, lastGameState_));
    const bool gameplayFrame = isGameplayFrame(lastGameState_, controllerOutput);

    if (gameplayFrame) {
        if (mode_ == Mode::HoldRightSearch) {
            plan_.frames.push_back(playerFrame);
        }
        else if (playbackFrameIndex_ < plan_.frames.size()) {
            playbackFrameIndex_++;
        }
    }

    if (evaluation.done
        || (mode_ == Mode::PlanPlayback && playbackFrameIndex_ >= plan_.frames.size())) {
        completed_ = true;
    }

    return SmbPlanExecutionTickResult{
        .completed = completed_,
        .frameAdvanced = true,
    };
}

void SmbPlanExecution::pauseSet(bool paused)
{
    paused_ = paused;
    progress_.paused = paused_;
}

void SmbPlanExecution::stop()
{
    completed_ = true;
}

bool SmbPlanExecution::isCompleted() const
{
    return completed_;
}

bool SmbPlanExecution::isPaused() const
{
    return paused_;
}

bool SmbPlanExecution::hasRenderableFrame() const
{
    return scenarioVideoFrame_.has_value();
}

const Api::Plan& SmbPlanExecution::getPlan() const
{
    return plan_;
}

const Api::SearchProgress& SmbPlanExecution::getProgress() const
{
    return progress_;
}

const std::optional<ScenarioVideoFrame>& SmbPlanExecution::getScenarioVideoFrame() const
{
    return scenarioVideoFrame_;
}

const WorldData& SmbPlanExecution::getWorldData() const
{
    return worldData_;
}

PlayerControlFrame SmbPlanExecution::holdRightFrame()
{
    return smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::Right);
}

void SmbPlanExecution::updateProgress(const SmbSearchEvaluatorSummary& evaluatorSummary)
{
    progress_.paused = paused_;
    progress_.bestFrontier = evaluatorSummary.bestFrontier;
    progress_.elapsedFrames = evaluatorSummary.gameplayFrames >= progressFrameOffset_
        ? (evaluatorSummary.gameplayFrames - progressFrameOffset_)
        : 0u;
    plan_.summary.bestFrontier = progress_.bestFrontier;
    plan_.summary.elapsedFrames = progress_.elapsedFrames;
}

} // namespace DirtSim::Server::SearchSupport
