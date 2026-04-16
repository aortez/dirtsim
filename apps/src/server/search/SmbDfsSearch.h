#pragma once

#include "core/RenderMessage.h"
#include "core/Result.h"
#include "core/Timers.h"
#include "core/WorldData.h"
#include "server/api/Plan.h"
#include "server/api/SearchProgress.h"
#include "server/search/SmbSearchCore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {

class NesSmolnesScenarioDriver;
struct NesSuperMarioBrosState;

namespace Server::SearchSupport {

struct SmbSearchRootFixture;

enum class SmbDfsSearchCompletionReason : uint8_t {
    Completed = 0,
    Stopped = 1,
    Error = 2,
};

struct SmbDfsSearchOptions {
    uint32_t maxSearchedNodeCount = 5'000;
    uint32_t stallFrameLimit = 120;
    bool velocityPruningEnabled = true;
    bool belowScreenPruningEnabled = true;
    bool fallingTranspositionPruningEnabled = true;
    bool groundedVerticalJumpPrioritizationEnabled = true;
    std::optional<uint64_t> stopAfterBestFrontier = std::nullopt;
};

struct SmbDfsSearchTickResult {
    bool completed = false;
    bool frameAdvanced = false;
    bool renderChanged = false;
    std::optional<std::string> error = std::nullopt;
};

enum class SmbDfsSearchTraceEventType : uint8_t {
    Backtracked = 0,
    CompletedBudgetExceeded = 1,
    CompletedExhausted = 2,
    CompletedMilestoneReached = 3,
    Error = 4,
    ExpandedAlive = 5,
    PrunedDead = 6,
    PrunedStalled = 7,
    PrunedVelocityStuck = 8,
    RootInitialized = 9,
    Stopped = 10,
    PrunedBelowScreen = 11,
    PrunedTransposition = 12,
};

struct SmbDfsSearchTraceEntry {
    SmbDfsSearchTraceEventType eventType = SmbDfsSearchTraceEventType::RootInitialized;
    size_t nodeIndex = 0;
    std::optional<size_t> parentIndex = std::nullopt;
    std::optional<SmbSearchLegalAction> action = std::nullopt;
    uint64_t gameplayFrame = 0;
    uint64_t frontier = 0;
    double evaluationScore = 0.0;
    uint64_t framesSinceProgress = 0;
    bool groundedVerticalJumpPriorityAction = false;
};

class SmbDfsSearch {
public:
    SmbDfsSearch(SmbDfsSearchOptions options = {});

    Result<std::monostate, std::string> startDfs();
    Result<std::monostate, std::string> startFromFixture(const SmbSearchRootFixture& fixture);

    SmbDfsSearchTickResult tick();

    void pauseSet(bool paused);
    void stop();

    bool hasPersistablePlan() const;
    bool hasRenderableFrame() const;
    bool isCompleted() const;
    bool isPaused() const;

    std::optional<SmbDfsSearchCompletionReason> getCompletionReason() const;
    const std::optional<std::string>& getCompletionErrorMessage() const;
    const Api::Plan& getPlan() const;
    const Api::SearchProgress& getProgress() const;
    const std::optional<ScenarioVideoFrame>& getScenarioVideoFrame() const;
    const std::vector<SmbDfsSearchTraceEntry>& getTrace() const;
    const WorldData& getWorldData() const;

private:
    struct DfsFrame {
        size_t nodeIndex = 0;
        uint8_t nextActionIndex = 0;
        SmbSearchActionOrdering actionOrdering = {};
    };

    struct FallingTranspositionEntry {
        uint64_t bestFrontier = 0;
        uint64_t framesSinceProgress = 0;
        uint64_t gameplayFrame = 0;
    };

    struct FallingTranspositionKey {
        int16_t nearestEnemyDx = 0;
        int16_t nearestEnemyDy = 0;
        int16_t secondNearestEnemyDx = 0;
        int16_t secondNearestEnemyDy = 0;
        int16_t verticalSpeed = 0;
        int8_t facingX = 0;
        int8_t horizontalSpeed = 0;
        int8_t movementX = 0;
        uint16_t absoluteX = 0;
        uint8_t level = 0;
        uint8_t playerYScreen = 0;
        uint8_t powerupState = 0;
        uint8_t world = 0;
        bool enemyPresent = false;
        bool secondEnemyPresent = false;

        bool operator==(const FallingTranspositionKey& other) const;
    };

    struct FallingTranspositionKeyHash {
        size_t operator()(const FallingTranspositionKey& key) const;
    };

    Result<std::monostate, std::string> initializeRuntime();
    Result<std::monostate, std::string> initializeRootNode(
        const SmolnesRuntime::Savestate& savestate,
        const SmbSearchEvaluatorSummary& evaluatorSummary,
        const SmolnesRuntime::MemorySnapshot* memorySnapshot,
        const std::optional<ScenarioVideoFrame>& scenarioVideoFrame);

    void completeWithError(const std::string& errorMessage);
    void completeWithTraceEvent(SmbDfsSearchTraceEventType eventType);
    std::optional<FallingTranspositionKey> buildFallingTranspositionKey(
        const NesSuperMarioBrosState& state,
        const SmbSearchEvaluatorSummary& evaluatorSummary) const;
    bool isFallingTranspositionPruned(
        const FallingTranspositionKey& key,
        const SmbSearchEvaluatorSummary& evaluatorSummary) const;
    void publishFallingTranspositionEntry(size_t nodeIndex);
    void rebuildBestPlan();
    void recordTrace(const SmbDfsSearchTraceEntry& entry);
    void releaseNodeHeavyData(size_t nodeIndex);
    void updateBestLeaf(size_t nodeIndex);
    void updateRenderableState(const SmbSearchNode& node);

    SmbDfsSearchOptions options_;
    std::unique_ptr<NesSmolnesScenarioDriver> driver_;
    Timers timers_;
    WorldData worldData_;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame_ = std::nullopt;
    Api::Plan plan_;
    Api::SearchProgress progress_;
    std::vector<PlayerControlFrame> rootPrefixFrames_;
    std::vector<SmbSearchNode> nodes_;
    std::vector<DfsFrame> dfsStack_;
    std::vector<std::optional<FallingTranspositionKey>> fallingTranspositionKeys_;
    std::unordered_map<
        FallingTranspositionKey,
        FallingTranspositionEntry,
        FallingTranspositionKeyHash>
        fallingTranspositionTable_;
    std::vector<SmbDfsSearchTraceEntry> trace_;
    std::optional<size_t> bestLeafIndex_ = std::nullopt;
    uint64_t bestFrontier_ = 0;
    double bestScore_ = 0.0;
    bool completed_ = false;
    bool paused_ = false;
    std::optional<SmbDfsSearchCompletionReason> completionReason_ = std::nullopt;
    std::optional<std::string> completionErrorMessage_ = std::nullopt;
};

} // namespace Server::SearchSupport
} // namespace DirtSim
