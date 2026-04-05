#include "SmbPlanExecution.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/UUID.h"
#include "core/input/PlayerControlFrame.h"
#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"

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
    worldData_ = WorldData{};
    worldData_.width = 256;
    worldData_.height = 240;
    scenarioVideoFrame_.reset();
    lastGameState_.reset();
    progress_ = Api::SearchProgress{};
    completed_ = false;
    paused_ = false;
    completionReason_.reset();
    completionErrorMessage_.reset();
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
        completionReason_ = SmbPlanExecutionCompletionReason::Error;
        completionErrorMessage_ = "SMB execution not initialized";
        return SmbPlanExecutionTickResult{
            .completed = true,
            .error = completionErrorMessage_,
        };
    }

    if (mode_ == Mode::PlanPlayback && playbackFrameIndex_ >= plan_.frames.size()) {
        completed_ = true;
        completionReason_ = SmbPlanExecutionCompletionReason::Completed;
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
        completionReason_ = SmbPlanExecutionCompletionReason::Error;
        completionErrorMessage_ = stepResult.lastError.empty()
            ? std::optional<std::string>("NES runtime stopped")
            : std::optional<std::string>(stepResult.lastError);
        return SmbPlanExecutionTickResult{
            .completed = true,
            .error = completionErrorMessage_,
        };
    }

    if (stepResult.advancedFrames == 0) {
        return {};
    }

    worldData_.timestep += static_cast<int32_t>(stepResult.advancedFrames);

    const auto evaluation = gameAdapter_->evaluateFrame(
        NesGameAdapterFrameInput{
            .advancedFrames = stepResult.advancedFrames,
            .controllerMask = controllerOutput.resolvedControllerMask,
            .paletteFrame =
                stepResult.paletteFrame.has_value() ? &stepResult.paletteFrame.value() : nullptr,
            .memorySnapshot = stepResult.memorySnapshot,
        });

    if (evaluation.gameState.has_value()) {
        lastGameState_ = evaluation.gameState;
    }

    updateProgress(evaluation.fitnessDetails);
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
        completionReason_ = SmbPlanExecutionCompletionReason::Completed;
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
    completionReason_ = SmbPlanExecutionCompletionReason::Stopped;
    completionErrorMessage_.reset();
}

bool SmbPlanExecution::hasPersistablePlan() const
{
    return !plan_.frames.empty();
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

std::optional<SmbPlanExecutionCompletionReason> SmbPlanExecution::getCompletionReason() const
{
    return completionReason_;
}

const std::optional<std::string>& SmbPlanExecution::getCompletionErrorMessage() const
{
    return completionErrorMessage_;
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

uint64_t SmbPlanExecution::encodeFrontier(const NesSuperMarioBrosFitnessSnapshot& snapshot)
{
    return (static_cast<uint64_t>(snapshot.bestStageIndex) << 16)
        | static_cast<uint64_t>(snapshot.bestAbsoluteX);
}

PlayerControlFrame SmbPlanExecution::holdRightFrame()
{
    return PlayerControlFrame{
        .xAxis = 127,
        .yAxis = 0,
        .buttons = 0,
    };
}

uint8_t SmbPlanExecution::playerControlFrameToNesMask(const PlayerControlFrame& frame)
{
    uint8_t mask = 0;
    if (frame.buttons & PlayerControlButtons::ButtonA) {
        mask |= NesPolicyLayout::ButtonA;
    }
    if (frame.buttons & PlayerControlButtons::ButtonB) {
        mask |= NesPolicyLayout::ButtonB;
    }
    if (frame.buttons & PlayerControlButtons::ButtonSelect) {
        mask |= NesPolicyLayout::ButtonSelect;
    }
    if (frame.buttons & PlayerControlButtons::ButtonStart) {
        mask |= NesPolicyLayout::ButtonStart;
    }
    if (frame.xAxis <= -64) {
        mask |= NesPolicyLayout::ButtonLeft;
    }
    else if (frame.xAxis >= 64) {
        mask |= NesPolicyLayout::ButtonRight;
    }
    if (frame.yAxis <= -64) {
        mask |= NesPolicyLayout::ButtonUp;
    }
    else if (frame.yAxis >= 64) {
        mask |= NesPolicyLayout::ButtonDown;
    }
    return mask;
}

void SmbPlanExecution::updateProgress(const NesFitnessDetails& fitnessDetails)
{
    progress_.paused = paused_;

    const auto* snapshot = std::get_if<NesSuperMarioBrosFitnessSnapshot>(&fitnessDetails);
    if (snapshot == nullptr) {
        return;
    }

    progress_.bestFrontier = encodeFrontier(*snapshot);
    progress_.searchedNodeCount = snapshot->gameplayFrames;
    plan_.summary.bestFrontier = progress_.bestFrontier;
    plan_.summary.elapsedFrames = snapshot->gameplayFrames;
}

} // namespace DirtSim::Server::SearchSupport
