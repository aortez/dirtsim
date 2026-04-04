#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/WorldData.h"
#include "server/api/Plan.h"
#include "server/api/SearchProgress.h"
#include "server/search/SmbSearchHarness.h"
#include "server/search/SmbSegmentBeamSearch.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace DirtSim::Server::SearchSupport {

enum class SmbSearchCompletionReason : uint8_t {
    StoppedByUser = 0,
    ReachedSegmentLimit = 1,
    NoFurtherProgress = 2,
    Error = 3,
};

struct SmbSearchExecutionTickResult {
    bool completed = false;
    bool frameAdvanced = false;
    std::optional<std::string> error = std::nullopt;
};

struct SmbSearchExecutionParams {
    uint32_t beamWidth = 4;
    uint32_t maxSegments = 4;
    uint32_t segmentFrameBudget = 12;
};

class SmbSearchExecution {
public:
    SmbSearchExecution();
    explicit SmbSearchExecution(const SmbSearchExecutionParams& params);

    Result<std::monostate, std::string> start();
    SmbSearchExecutionTickResult tick();

    void pauseSet(bool paused);
    void stop();

    bool isCompleted() const;
    bool isPaused() const;
    bool hasRenderableFrame() const;
    bool hasPersistablePlan() const;

    std::optional<SmbSearchCompletionReason> getCompletionReason() const;
    const std::optional<std::string>& getCompletionErrorMessage() const;
    const Api::Plan& getPlan() const;
    const Api::SearchProgress& getProgress() const;
    const std::optional<ScenarioVideoFrame>& getScenarioVideoFrame() const;
    const WorldData& getWorldData() const;

private:
    struct CommittedCheckpoint {
        SmbSearchRootFixture root;
        std::vector<PlayerControlFrame> framesFromParent;
        uint64_t nextCandidateIndex = 0;
    };

    void accumulateCurrentSegmentExpandedNodes();
    void captureCompletedSegmentProgress();
    Result<std::monostate, std::string> beginNextSegment();
    void backtrackOneCheckpoint();
    Result<bool, std::string> finalizeCurrentSegment();
    std::optional<size_t> getActiveNodeIndex() const;
    bool setActiveRenderFrame(const std::optional<size_t>& nodeIndex);
    void updateProgress();

    SmbSearchExecutionParams params_;
    SmbSearchHarness harness_;
    SmbSegmentBeamSearch segmentSearch_;
    std::optional<SmbSegmentBeamSearchState> currentSegmentState_ = std::nullopt;
    std::vector<CommittedCheckpoint> committedCheckpoints_;
    Api::Plan plan_;
    Api::SearchProgress progress_;
    std::vector<PlayerControlFrame> currentPlanFrames_;
    WorldData worldData_;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame_ = std::nullopt;
    std::optional<SmbSearchCompletionReason> completionReason_ = std::nullopt;
    std::optional<std::string> completionErrorMessage_ = std::nullopt;
    uint32_t completedCandidateCount_ = 0u;
    uint32_t completedCandidateIndex_ = 0u;
    uint32_t completedSegmentIndex_ = 0u;
    uint32_t completedStepIndex_ = 0u;
    uint64_t backtrackCount_ = 0;
    uint64_t completedExpandedNodeCount_ = 0;
    uint64_t segmentAttempts_ = 0;
    uint64_t successfulSegments_ = 0;
    bool completed_ = false;
    bool paused_ = false;
    bool stopRequested_ = false;
};

} // namespace DirtSim::Server::SearchSupport
