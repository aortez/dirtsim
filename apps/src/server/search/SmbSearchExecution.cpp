#include "server/search/SmbSearchExecution.h"

#include "core/Assert.h"
#include "core/UUID.h"

#include <algorithm>

namespace DirtSim::Server::SearchSupport {

namespace {

constexpr int16_t kDefaultVideoWidth = 256;
constexpr int16_t kDefaultVideoHeight = 240;

Api::SmbPlaybackRoot makePlaybackRoot(const SmbSearchRootFixture& root)
{
    Api::SmbPlaybackRoot playbackRoot{};
    playbackRoot.savestateFrameId = root.savestate.frameId;
    playbackRoot.savestateBytes.reserve(root.savestate.bytes.size());
    for (const std::byte value : root.savestate.bytes) {
        playbackRoot.savestateBytes.push_back(static_cast<uint8_t>(value));
    }
    playbackRoot.bestFrontier = root.evaluatorSummary.bestFrontier;
    playbackRoot.gameplayFrames = root.evaluatorSummary.gameplayFrames;
    playbackRoot.gameplayFramesSinceProgress = root.evaluatorSummary.gameplayFramesSinceProgress;
    playbackRoot.distanceRewardTotal = root.evaluatorSummary.distanceRewardTotal;
    playbackRoot.evaluationScore = root.evaluatorSummary.evaluationScore;
    playbackRoot.levelClearRewardTotal = root.evaluatorSummary.levelClearRewardTotal;
    playbackRoot.gameState = root.evaluatorSummary.gameState;
    playbackRoot.terminal = root.evaluatorSummary.terminal;
    return playbackRoot;
}

} // namespace

SmbSearchExecution::SmbSearchExecution() : SmbSearchExecution(SmbSearchExecutionParams{})
{}

SmbSearchExecution::SmbSearchExecution(const SmbSearchExecutionParams& params) : params_(params)
{}

Result<std::monostate, std::string> SmbSearchExecution::start()
{
    currentSegmentState_.reset();
    committedCheckpoints_.clear();
    currentPlanFrames_.clear();
    worldData_ = WorldData{};
    worldData_.width = kDefaultVideoWidth;
    worldData_.height = kDefaultVideoHeight;
    scenarioVideoFrame_.reset();
    progress_ = Api::SearchProgress{};
    completionReason_.reset();
    completionErrorMessage_.reset();
    completedCandidateCount_ = 0u;
    completedCandidateIndex_ = 0u;
    completedSegmentIndex_ = 0u;
    completedStepIndex_ = 0u;
    backtrackCount_ = 0u;
    completedExpandedNodeCount_ = 0u;
    segmentAttempts_ = 0u;
    successfulSegments_ = 0u;
    completed_ = false;
    paused_ = false;
    stopRequested_ = false;

    plan_ = Api::Plan{};
    plan_.summary.id = UUID::generate();

    const auto initialRootResult =
        harness_.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    if (initialRootResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to capture SMB initial search root: " + initialRootResult.errorValue());
    }

    committedCheckpoints_.push_back(
        CommittedCheckpoint{
            .root = initialRootResult.value(),
            .framesFromParent = {},
            .nextCandidateIndex = 0u,
        });
    plan_.smbPlaybackRoot = makePlaybackRoot(committedCheckpoints_.back().root);

    scenarioVideoFrame_ = committedCheckpoints_.back().root.scenarioVideoFrame;
    if (scenarioVideoFrame_.has_value()) {
        worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
        worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
    }

    const auto beginResult = beginNextSegment();
    if (beginResult.isError()) {
        return Result<std::monostate, std::string>::error(beginResult.errorValue());
    }

    updateProgress();
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

SmbSearchExecutionTickResult SmbSearchExecution::tick()
{
    if (completed_) {
        return SmbSearchExecutionTickResult{
            .completed = true,
        };
    }

    if (paused_) {
        updateProgress();
        return {};
    }

    if (stopRequested_) {
        completionReason_ = SmbSearchCompletionReason::StoppedByUser;
        completed_ = true;
        currentSegmentState_.reset();
        captureCompletedSegmentProgress();
        updateProgress();
        return SmbSearchExecutionTickResult{
            .completed = true,
        };
    }

    if (!currentSegmentState_.has_value()) {
        completionReason_ = SmbSearchCompletionReason::Error;
        completionErrorMessage_ = "SMB search execution is missing an active segment";
        completed_ = true;
        updateProgress();
        return SmbSearchExecutionTickResult{
            .completed = true,
            .error = completionErrorMessage_,
        };
    }

    const auto tickResult = segmentSearch_.tick(
        currentSegmentState_.value(),
        SmbSegmentBeamSearchParams{
            .beamWidth = params_.beamWidth,
            .segmentFrameBudget = params_.segmentFrameBudget,
        });
    if (tickResult.isError()) {
        completionReason_ = SmbSearchCompletionReason::Error;
        completionErrorMessage_ = tickResult.errorValue();
        completed_ = true;
        captureCompletedSegmentProgress();
        currentSegmentState_.reset();
        updateProgress();
        return SmbSearchExecutionTickResult{
            .completed = true,
            .error = completionErrorMessage_,
        };
    }

    const auto activeNodeIndex = getActiveNodeIndex();
    const bool frameAdvanced = setActiveRenderFrame(activeNodeIndex);
    updateProgress();

    if (!tickResult.value().completed) {
        return SmbSearchExecutionTickResult{
            .frameAdvanced = frameAdvanced,
        };
    }

    const auto finalizeResult = finalizeCurrentSegment();
    if (finalizeResult.isError()) {
        completionReason_ = SmbSearchCompletionReason::Error;
        completionErrorMessage_ = finalizeResult.errorValue();
        completed_ = true;
        captureCompletedSegmentProgress();
        currentSegmentState_.reset();
        updateProgress();
        return SmbSearchExecutionTickResult{
            .completed = true,
            .frameAdvanced = frameAdvanced,
            .error = completionErrorMessage_,
        };
    }

    const bool promotedCheckpoint = finalizeResult.value();
    if (promotedCheckpoint) {
        if (params_.maxSegments > 0u && successfulSegments_ >= params_.maxSegments) {
            completionReason_ = SmbSearchCompletionReason::ReachedSegmentLimit;
            completed_ = true;
            captureCompletedSegmentProgress();
            currentSegmentState_.reset();
            updateProgress();
            return SmbSearchExecutionTickResult{
                .completed = true,
                .frameAdvanced = frameAdvanced,
            };
        }

        const auto beginResult = beginNextSegment();
        if (beginResult.isError()) {
            completionReason_ = SmbSearchCompletionReason::Error;
            completionErrorMessage_ = beginResult.errorValue();
            completed_ = true;
            captureCompletedSegmentProgress();
            currentSegmentState_.reset();
            updateProgress();
            return SmbSearchExecutionTickResult{
                .completed = true,
                .frameAdvanced = frameAdvanced,
                .error = completionErrorMessage_,
            };
        }

        updateProgress();
        return SmbSearchExecutionTickResult{
            .frameAdvanced = frameAdvanced,
        };
    }

    if (committedCheckpoints_.size() == 1u) {
        completionReason_ = SmbSearchCompletionReason::NoFurtherProgress;
        completed_ = true;
        captureCompletedSegmentProgress();
        currentSegmentState_.reset();
        updateProgress();
        return SmbSearchExecutionTickResult{
            .completed = true,
            .frameAdvanced = frameAdvanced,
        };
    }

    backtrackOneCheckpoint();
    const auto beginResult = beginNextSegment();
    if (beginResult.isError()) {
        completionReason_ = SmbSearchCompletionReason::Error;
        completionErrorMessage_ = beginResult.errorValue();
        completed_ = true;
        captureCompletedSegmentProgress();
        currentSegmentState_.reset();
        updateProgress();
        return SmbSearchExecutionTickResult{
            .completed = true,
            .frameAdvanced = frameAdvanced,
            .error = completionErrorMessage_,
        };
    }

    updateProgress();
    return SmbSearchExecutionTickResult{
        .frameAdvanced = frameAdvanced,
    };
}

void SmbSearchExecution::pauseSet(bool paused)
{
    paused_ = paused;
    updateProgress();
}

void SmbSearchExecution::stop()
{
    stopRequested_ = true;
}

bool SmbSearchExecution::isCompleted() const
{
    return completed_;
}

bool SmbSearchExecution::isPaused() const
{
    return paused_;
}

bool SmbSearchExecution::hasRenderableFrame() const
{
    return scenarioVideoFrame_.has_value();
}

bool SmbSearchExecution::hasPersistablePlan() const
{
    return !plan_.frames.empty();
}

std::optional<SmbSearchCompletionReason> SmbSearchExecution::getCompletionReason() const
{
    return completionReason_;
}

const std::optional<std::string>& SmbSearchExecution::getCompletionErrorMessage() const
{
    return completionErrorMessage_;
}

const Api::Plan& SmbSearchExecution::getPlan() const
{
    return plan_;
}

const Api::SearchProgress& SmbSearchExecution::getProgress() const
{
    return progress_;
}

const std::optional<ScenarioVideoFrame>& SmbSearchExecution::getScenarioVideoFrame() const
{
    return scenarioVideoFrame_;
}

const WorldData& SmbSearchExecution::getWorldData() const
{
    return worldData_;
}

void SmbSearchExecution::accumulateCurrentSegmentExpandedNodes()
{
    if (!currentSegmentState_.has_value()) {
        return;
    }

    completedExpandedNodeCount_ += currentSegmentState_->expandedNodeCount;
}

void SmbSearchExecution::captureCompletedSegmentProgress()
{
    if (!currentSegmentState_.has_value()) {
        return;
    }

    completedCandidateCount_ = static_cast<uint32_t>(
        std::min<uint64_t>(currentSegmentState_->totalCandidateCount, UINT32_MAX));
    completedCandidateIndex_ = static_cast<uint32_t>(
        std::min<uint64_t>(currentSegmentState_->nextCandidateIndex, UINT32_MAX));
    completedStepIndex_ = currentSegmentState_->completedSteps;
    completedSegmentIndex_ = static_cast<uint32_t>(successfulSegments_);
}

Result<std::monostate, std::string> SmbSearchExecution::beginNextSegment()
{
    if (committedCheckpoints_.empty()) {
        return Result<std::monostate, std::string>::error(
            "Cannot begin SMB search segment without a committed checkpoint");
    }

    const CommittedCheckpoint& checkpoint = committedCheckpoints_.back();
    const auto stateResult = segmentSearch_.start(checkpoint.root, checkpoint.nextCandidateIndex);
    if (stateResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to start SMB segment search: " + stateResult.errorValue());
    }

    currentSegmentState_ = std::move(stateResult).value();
    segmentAttempts_ += 1u;
    scenarioVideoFrame_ = checkpoint.root.scenarioVideoFrame;
    if (scenarioVideoFrame_.has_value()) {
        worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
        worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
    }
    setActiveRenderFrame(std::optional<size_t>(0u));
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void SmbSearchExecution::backtrackOneCheckpoint()
{
    DIRTSIM_ASSERT(
        committedCheckpoints_.size() > 1u, "SMB search backtrack requires a prior checkpoint");

    backtrackCount_ += 1u;
    const auto removedFrames = committedCheckpoints_.back().framesFromParent.size();
    committedCheckpoints_.pop_back();

    if (removedFrames <= currentPlanFrames_.size()) {
        currentPlanFrames_.resize(currentPlanFrames_.size() - removedFrames);
    }
    else {
        currentPlanFrames_.clear();
    }

    successfulSegments_ = committedCheckpoints_.size() - 1u;
    scenarioVideoFrame_ = committedCheckpoints_.back().root.scenarioVideoFrame;
    if (scenarioVideoFrame_.has_value()) {
        worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
        worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
    }
}

Result<bool, std::string> SmbSearchExecution::finalizeCurrentSegment()
{
    DIRTSIM_ASSERT(
        currentSegmentState_.has_value(), "SMB search finalize requires an active state");

    CommittedCheckpoint& currentCheckpoint = committedCheckpoints_.back();
    currentCheckpoint.nextCandidateIndex = currentSegmentState_->nextCandidateIndex;

    captureCompletedSegmentProgress();
    accumulateCurrentSegmentExpandedNodes();

    if (!currentSegmentState_->bestNodeIndex.has_value()) {
        currentSegmentState_.reset();
        return Result<bool, std::string>::okay(false);
    }

    const size_t bestNodeIndex = currentSegmentState_->bestNodeIndex.value();
    if (bestNodeIndex >= currentSegmentState_->nodes.size()) {
        return Result<bool, std::string>::error(
            "SMB segment search returned an out-of-range best node index");
    }

    const SmbSearchNode& bestNode = currentSegmentState_->nodes[bestNodeIndex];
    if (!bestNode.checkpointEligible
        || bestNode.evaluatorSummary.evaluationScore <= currentSegmentState_->rootEvaluationScore) {
        return Result<bool, std::string>::error(
            "SMB segment search returned a non-promotable best node");
    }

    const auto segmentFramesResult =
        reconstructPlanFrames(currentSegmentState_->nodes, bestNodeIndex);
    if (segmentFramesResult.isError()) {
        return Result<bool, std::string>::error(
            "Failed to reconstruct promoted SMB segment: " + segmentFramesResult.errorValue());
    }

    const std::vector<PlayerControlFrame>& segmentFrames = segmentFramesResult.value();
    currentPlanFrames_.insert(currentPlanFrames_.end(), segmentFrames.begin(), segmentFrames.end());

    SmbSearchRootFixture promotedRoot{};
    promotedRoot.id = currentCheckpoint.root.id;
    promotedRoot.evaluatorSummary = bestNode.evaluatorSummary;
    promotedRoot.memorySnapshot = bestNode.memorySnapshot;
    promotedRoot.scenarioVideoFrame = bestNode.scenarioVideoFrame;
    promotedRoot.savestate = bestNode.savestate;
    promotedRoot.name =
        currentCheckpoint.root.name + "_segment_" + std::to_string(committedCheckpoints_.size());

    committedCheckpoints_.push_back(
        CommittedCheckpoint{
            .root = promotedRoot,
            .framesFromParent = segmentFrames,
            .nextCandidateIndex = 0u,
        });
    successfulSegments_ = committedCheckpoints_.size() - 1u;

    if (plan_.frames.empty()
        || plan_.summary.bestFrontier < promotedRoot.evaluatorSummary.bestFrontier) {
        plan_.frames = currentPlanFrames_;
        plan_.summary.elapsedFrames = static_cast<uint64_t>(plan_.frames.size());
        plan_.summary.bestFrontier = promotedRoot.evaluatorSummary.bestFrontier;
    }

    currentSegmentState_.reset();
    scenarioVideoFrame_ = promotedRoot.scenarioVideoFrame;
    if (scenarioVideoFrame_.has_value()) {
        worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
        worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
    }

    return Result<bool, std::string>::okay(true);
}

std::optional<size_t> SmbSearchExecution::getActiveNodeIndex() const
{
    if (!currentSegmentState_.has_value()) {
        return std::nullopt;
    }
    if (!currentSegmentState_->frontierNodeIndices.empty()) {
        return currentSegmentState_->frontierNodeIndices.front();
    }
    if (!currentSegmentState_->nodes.empty()) {
        return std::optional<size_t>(0u);
    }
    return std::nullopt;
}

bool SmbSearchExecution::setActiveRenderFrame(const std::optional<size_t>& nodeIndex)
{
    if (!currentSegmentState_.has_value() || !nodeIndex.has_value()) {
        return false;
    }
    if (nodeIndex.value() >= currentSegmentState_->nodes.size()) {
        return false;
    }

    const SmbSearchNode& node = currentSegmentState_->nodes[nodeIndex.value()];
    if (!node.scenarioVideoFrame.has_value()) {
        return false;
    }

    const bool changed = !scenarioVideoFrame_.has_value()
        || scenarioVideoFrame_->pixels != node.scenarioVideoFrame->pixels;
    scenarioVideoFrame_ = node.scenarioVideoFrame;
    worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
    worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
    worldData_.timestep = static_cast<int32_t>(currentPlanFrames_.size() + node.depth);
    return changed;
}

void SmbSearchExecution::updateProgress()
{
    progress_.paused = paused_;
    progress_.attemptIndex =
        static_cast<uint32_t>(std::min<uint64_t>(segmentAttempts_, UINT32_MAX));
    progress_.backtrackCount =
        static_cast<uint32_t>(std::min<uint64_t>(backtrackCount_, UINT32_MAX));
    progress_.beamWidth = params_.beamWidth;
    progress_.maxSegments = params_.maxSegments;
    progress_.maxSteps = params_.segmentFrameBudget;

    uint64_t activeFrontier = committedCheckpoints_.empty()
        ? 0u
        : committedCheckpoints_.back().root.evaluatorSummary.bestFrontier;
    uint64_t activeElapsedFrames = currentPlanFrames_.size();
    uint64_t activeExpandedNodeCount = completedExpandedNodeCount_;
    uint32_t activeSegmentIndex = static_cast<uint32_t>(successfulSegments_);
    uint32_t activeCandidateCount = completedCandidateCount_;
    uint32_t activeCandidateIndex = completedCandidateIndex_;
    uint32_t activeStepIndex = completedStepIndex_;

    if (currentSegmentState_.has_value()) {
        activeExpandedNodeCount += currentSegmentState_->expandedNodeCount;
        activeSegmentIndex = static_cast<uint32_t>(committedCheckpoints_.size());
        activeCandidateCount = static_cast<uint32_t>(
            std::min<uint64_t>(currentSegmentState_->totalCandidateCount, UINT32_MAX));
        activeCandidateIndex = static_cast<uint32_t>(
            std::min<uint64_t>(currentSegmentState_->nextCandidateIndex, UINT32_MAX));
        activeStepIndex = currentSegmentState_->completedSteps;

        if (const auto activeNodeIndex = getActiveNodeIndex(); activeNodeIndex.has_value()
            && activeNodeIndex.value() < currentSegmentState_->nodes.size()) {
            const SmbSearchNode& node = currentSegmentState_->nodes[activeNodeIndex.value()];
            activeFrontier = node.evaluatorSummary.bestFrontier;
            activeElapsedFrames = currentPlanFrames_.size() + node.depth;
        }
    }
    else if (completed_) {
        activeSegmentIndex = completedSegmentIndex_;
        activeCandidateCount = completedCandidateCount_;
        activeCandidateIndex = completedCandidateIndex_;
        activeStepIndex = completedStepIndex_;
        if (!committedCheckpoints_.empty()) {
            activeFrontier = committedCheckpoints_.back().root.evaluatorSummary.bestFrontier;
            activeElapsedFrames = currentPlanFrames_.size();
        }
    }

    progress_.elapsedFrames = activeElapsedFrames;
    progress_.expandedNodeCount = activeExpandedNodeCount;
    progress_.segmentIndex = activeSegmentIndex;
    progress_.candidateCount = activeCandidateCount;
    progress_.candidateIndex = activeCandidateIndex;
    progress_.stepIndex = activeStepIndex;
    progress_.bestFrontier = std::max(activeFrontier, plan_.summary.bestFrontier);
}

} // namespace DirtSim::Server::SearchSupport
