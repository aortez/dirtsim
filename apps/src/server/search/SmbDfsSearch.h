#pragma once

#include "core/RenderMessage.h"
#include "core/Result.h"
#include "core/Timers.h"
#include "core/WorldData.h"
#include "server/api/Plan.h"
#include "server/api/SearchProgress.h"
#include "server/search/SmbSearchCore.h"

#include <array>
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

constexpr uint8_t kDefaultFallingTranspositionPlayerYScreenThreshold = 0u;
constexpr size_t kClosedLossTranspositionRamByteCount = 63u;

// Hidden movement, input, collision, scroll, and timer bytes used to keep TT probes exact.
constexpr std::array<size_t, kClosedLossTranspositionRamByteCount>
    kClosedLossTranspositionRamByteAddrs{
        0x0000, 0x0003, 0x000A, 0x000B, 0x000E, 0x001D, 0x0057, 0x009F, 0x00B5, 0x00CE, 0x03AD,
        0x03B8, 0x0400, 0x0416, 0x0433, 0x0490, 0x04AC, 0x04AD, 0x04AE, 0x04AF, 0x06D5, 0x06FC,
        0x0700, 0x0701, 0x0702, 0x0704, 0x0705, 0x0709, 0x070A, 0x070C, 0x070D, 0x0712, 0x0714,
        0x071A, 0x071B, 0x071C, 0x071D, 0x071E, 0x071F, 0x0722, 0x0723, 0x072C, 0x072F, 0x0739,
        0x0747, 0x074A, 0x0752, 0x0753, 0x0755, 0x0775, 0x077F, 0x0781, 0x0782, 0x0783, 0x0784,
        0x0785, 0x0786, 0x0789, 0x078A, 0x0791, 0x0795, 0x079E, 0x079F,
    };

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
    uint8_t fallingTranspositionPlayerYScreenThreshold =
        kDefaultFallingTranspositionPlayerYScreenThreshold;
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
    PrunedNonGameplay = 13,
};

enum class SmbDfsClosedLossBlockReason : uint8_t {
    None = 0,
    FreshProgress = 1,
    NonGameplay = 2,
    ChildNotClosedLoss = 3,
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
    bool closedLossCandidate = false;
    bool closedLossProven = false;
    bool closedLossApproximateTranspositionPruned = false;
    uint8_t closedLossApproximateTranspositionRamDiffCount = 0u;
    std::array<bool, kClosedLossTranspositionRamByteCount>
        closedLossApproximateTranspositionRamDiffs{};
    SmbDfsClosedLossBlockReason closedLossBlockReason = SmbDfsClosedLossBlockReason::None;
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
        bool closedLossCandidate = true;
        SmbDfsClosedLossBlockReason closedLossBlockReason = SmbDfsClosedLossBlockReason::None;
    };

    struct ClosedLossTranspositionEntry {
        uint64_t bestFrontier = 0;
        uint64_t framesSinceProgress = 0;
        uint64_t gameplayFrame = 0;
    };

    struct ClosedLossTranspositionShapeEntry {
        ClosedLossTranspositionEntry proof = {};
        std::array<uint8_t, kClosedLossTranspositionRamByteCount> ramBytes{};
    };

    struct ClosedLossTranspositionKey {
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
        std::array<uint8_t, kClosedLossTranspositionRamByteCount> ramBytes{};
        bool enemyPresent = false;
        bool secondEnemyPresent = false;

        bool operator==(const ClosedLossTranspositionKey& other) const;
    };

    struct ClosedLossTranspositionKeyHash {
        size_t operator()(const ClosedLossTranspositionKey& key) const;
    };

    struct ClosedLossTranspositionShapeKey {
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

        bool operator==(const ClosedLossTranspositionShapeKey& other) const;
    };

    struct ClosedLossTranspositionShapeKeyHash {
        size_t operator()(const ClosedLossTranspositionShapeKey& key) const;
    };

    struct ClosedLossTranspositionShapeProbeResult {
        bool pruned = false;
        uint8_t ramDiffCount = 0u;
        std::array<bool, kClosedLossTranspositionRamByteCount> ramDiffs{};
    };

    enum class SmbSearchNodeOutcome : uint8_t {
        Open = 0,
        ClosedLoss = 1,
        NotClosedLoss = 2,
    };

    Result<std::monostate, std::string> initializeRuntime();
    Result<std::monostate, std::string> initializeRootNode(
        const SmolnesRuntime::Savestate& savestate,
        const SmbSearchEvaluatorSummary& evaluatorSummary,
        const SmolnesRuntime::MemorySnapshot* memorySnapshot,
        const std::optional<ScenarioVideoFrame>& scenarioVideoFrame);

    void completeWithError(const std::string& errorMessage);
    void completeWithTraceEvent(SmbDfsSearchTraceEventType eventType);
    bool isClosedLossCandidate(size_t nodeIndex) const;
    std::optional<ClosedLossTranspositionKey> buildClosedLossTranspositionKey(
        const NesSuperMarioBrosState& state,
        const SmolnesRuntime::MemorySnapshot& memorySnapshot) const;
    ClosedLossTranspositionShapeKey buildClosedLossTranspositionShapeKey(
        const ClosedLossTranspositionKey& key) const;
    void noteChildOutcome(
        DfsFrame& dfsFrame,
        SmbSearchNodeOutcome childOutcome,
        SmbDfsClosedLossBlockReason childBlockReason);
    SmbDfsClosedLossBlockReason getInitialClosedLossBlockReason(size_t nodeIndex) const;
    bool isClosedLossTranspositionPruned(
        const ClosedLossTranspositionKey& key,
        const SmbSearchEvaluatorSummary& evaluatorSummary) const;
    ClosedLossTranspositionShapeProbeResult probeClosedLossTranspositionShape(
        const ClosedLossTranspositionShapeKey& key,
        const ClosedLossTranspositionKey& exactKey,
        const SmbSearchEvaluatorSummary& evaluatorSummary) const;
    void publishClosedLossTranspositionEntry(size_t nodeIndex);
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
    std::vector<SmbSearchNodeOutcome> nodeOutcomes_;
    std::vector<SmbDfsClosedLossBlockReason> closedLossBlockReasons_;
    std::vector<std::optional<ClosedLossTranspositionKey>> closedLossTranspositionKeys_;
    std::unordered_map<
        ClosedLossTranspositionKey,
        ClosedLossTranspositionEntry,
        ClosedLossTranspositionKeyHash>
        closedLossTranspositionTable_;
    std::unordered_map<
        ClosedLossTranspositionShapeKey,
        ClosedLossTranspositionShapeEntry,
        ClosedLossTranspositionShapeKeyHash>
        closedLossTranspositionShapeTable_;
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
