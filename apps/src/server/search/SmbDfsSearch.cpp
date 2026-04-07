#include "server/search/SmbDfsSearch.h"

#include "core/Assert.h"
#include "core/ScenarioConfig.h"
#include "core/UUID.h"
#include "core/scenarios/nes/NesGameAdapter.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"
#include "server/search/SmbSearchHarness.h"

#include <algorithm>
#include <cmath>

namespace DirtSim::Server::SearchSupport {

namespace {

constexpr uint32_t kLevelsPerWorld = 4u;
constexpr uint8_t kVelocityPruneConsecutiveFrameThreshold = 2u;
constexpr double kVelocityPruneHorizontalSpeedEpsilon = 0.05;

std::unique_ptr<NesGameAdapter> createSmbGameAdapter()
{
    return NesGameAdapterRegistry::createDefault().createAdapter(
        Scenario::EnumType::NesSuperMarioBros);
}

uint64_t encodeCurrentFrontier(const NesSuperMarioBrosState& state)
{
    const uint32_t stageIndex =
        (static_cast<uint32_t>(state.world) * kLevelsPerWorld) + state.level;
    return encodeSmbFrontier(stageIndex, state.absoluteX);
}

Api::SearchProgressEvent toSearchProgressEvent(SmbDfsSearchTraceEventType eventType)
{
    switch (eventType) {
        case SmbDfsSearchTraceEventType::Backtracked:
            return Api::SearchProgressEvent::Backtracked;
        case SmbDfsSearchTraceEventType::CompletedBudgetExceeded:
            return Api::SearchProgressEvent::CompletedBudgetExceeded;
        case SmbDfsSearchTraceEventType::CompletedExhausted:
            return Api::SearchProgressEvent::CompletedExhausted;
        case SmbDfsSearchTraceEventType::CompletedMilestoneReached:
            return Api::SearchProgressEvent::CompletedMilestoneReached;
        case SmbDfsSearchTraceEventType::Error:
            return Api::SearchProgressEvent::Error;
        case SmbDfsSearchTraceEventType::ExpandedAlive:
            return Api::SearchProgressEvent::ExpandedAlive;
        case SmbDfsSearchTraceEventType::PrunedDead:
            return Api::SearchProgressEvent::PrunedDead;
        case SmbDfsSearchTraceEventType::PrunedStalled:
            return Api::SearchProgressEvent::PrunedStalled;
        case SmbDfsSearchTraceEventType::PrunedVelocityStuck:
            return Api::SearchProgressEvent::PrunedVelocityStuck;
        case SmbDfsSearchTraceEventType::RootInitialized:
            return Api::SearchProgressEvent::RootInitialized;
        case SmbDfsSearchTraceEventType::Stopped:
            return Api::SearchProgressEvent::Stopped;
    }

    DIRTSIM_ASSERT(false, "Unhandled SmbDfsSearchTraceEventType");
    return Api::SearchProgressEvent::Unknown;
}

} // namespace

SmbDfsSearch::SmbDfsSearch(SmbDfsSearchOptions options) : options_(options)
{}

Result<std::monostate, std::string> SmbDfsSearch::startDfs()
{
    const auto initResult = initializeRuntime();
    if (initResult.isError()) {
        return initResult;
    }

    auto gameAdapter = createSmbGameAdapter();
    if (!gameAdapter) {
        return Result<std::monostate, std::string>::error("Failed to create SMB game adapter");
    }

    gameAdapter->reset(driver_->getRuntimeResolvedRomId());

    const PlayerControlFrame setupFrame =
        smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::RightRun);
    std::optional<uint8_t> lastGameState = std::nullopt;

    for (uint64_t stepIndex = 0; stepIndex < 2000u; ++stepIndex) {
        const NesGameAdapterControllerOutput controllerOutput = gameAdapter->resolveControllerMask(
            NesGameAdapterControllerInput{
                .inferredControllerMask = playerControlFrameToNesMask(setupFrame),
                .lastGameState = lastGameState,
            });

        const auto stepResult = driver_->step(timers_, controllerOutput.resolvedControllerMask);
        if (!stepResult.runtimeHealthy || !stepResult.runtimeRunning) {
            const std::string errorMessage = stepResult.lastError.empty()
                ? "NES runtime stopped during DFS root capture"
                : stepResult.lastError;
            return Result<std::monostate, std::string>::error(errorMessage);
        }
        if (stepResult.advancedFrames == 0) {
            continue;
        }

        const auto evaluation = gameAdapter->evaluateFrame(
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

        if (!isSmbGameplayFrame(lastGameState, controllerOutput)) {
            continue;
        }

        const auto savestate = driver_->copyRuntimeSavestate();
        if (!savestate.has_value()) {
            return Result<std::monostate, std::string>::error(
                "Failed to capture SMB DFS root savestate");
        }

        const SmbSearchEvaluatorSummary evaluatorSummary =
            buildSmbSearchEvaluatorSummary(evaluation.fitnessDetails, lastGameState);
        return initializeRootNode(
            savestate.value(),
            evaluatorSummary,
            stepResult.memorySnapshot.has_value() ? &stepResult.memorySnapshot.value() : nullptr,
            stepResult.scenarioVideoFrame);
    }

    return Result<std::monostate, std::string>::error("Timed out while preparing SMB DFS root");
}

Result<std::monostate, std::string> SmbDfsSearch::startFromFixture(
    const SmbSearchRootFixture& fixture)
{
    const auto initResult = initializeRuntime();
    if (initResult.isError()) {
        return initResult;
    }

    if (!driver_->loadRuntimeSavestate(fixture.savestate, 2000u)) {
        const std::string runtimeLastError = driver_->getRuntimeLastError();
        return Result<std::monostate, std::string>::error(
            runtimeLastError.empty() ? "Failed to load SMB DFS fixture savestate"
                                     : runtimeLastError);
    }

    return initializeRootNode(
        fixture.savestate,
        fixture.evaluatorSummary,
        fixture.memorySnapshot.has_value() ? &fixture.memorySnapshot.value() : nullptr,
        fixture.scenarioVideoFrame.has_value() ? fixture.scenarioVideoFrame
                                               : driver_->copyRuntimeFrameSnapshot());
}

SmbDfsSearchTickResult SmbDfsSearch::tick()
{
    if (completed_) {
        return SmbDfsSearchTickResult{
            .completed = true,
            .error = completionReason_ == SmbDfsSearchCompletionReason::Error
                ? completionErrorMessage_
                : std::nullopt,
        };
    }

    if (paused_) {
        return {};
    }

    if (!driver_) {
        completeWithError("SMB DFS search not initialized");
        return SmbDfsSearchTickResult{
            .completed = true,
            .error = completionErrorMessage_,
        };
    }

    while (true) {
        if (options_.maxSearchedNodeCount > 0
            && progress_.searchedNodeCount >= options_.maxSearchedNodeCount) {
            completeWithTraceEvent(SmbDfsSearchTraceEventType::CompletedBudgetExceeded);
            return SmbDfsSearchTickResult{ .completed = true };
        }

        if (dfsStack_.empty()) {
            completeWithTraceEvent(SmbDfsSearchTraceEventType::CompletedExhausted);
            return SmbDfsSearchTickResult{ .completed = true };
        }

        DfsFrame& dfsFrame = dfsStack_.back();
        if (dfsFrame.nextActionIndex >= dfsFrame.actionOrdering.count) {
            const SmbSearchNode& exhaustedNode = nodes_[dfsFrame.nodeIndex];
            recordTrace(
                SmbDfsSearchTraceEntry{
                    .eventType = SmbDfsSearchTraceEventType::Backtracked,
                    .nodeIndex = dfsFrame.nodeIndex,
                    .parentIndex = exhaustedNode.parentIndex,
                    .action = exhaustedNode.actionFromParent,
                    .gameplayFrame = exhaustedNode.gameplayFrame,
                    .frontier = exhaustedNode.evaluatorSummary.bestFrontier,
                    .evaluationScore = exhaustedNode.evaluatorSummary.evaluationScore,
                    .framesSinceProgress =
                        exhaustedNode.evaluatorSummary.gameplayFramesSinceProgress,
                });
            const size_t poppedNodeIndex = dfsFrame.nodeIndex;
            dfsStack_.pop_back();
            releaseNodeHeavyData(poppedNodeIndex);
            progress_.lastSearchEvent = Api::SearchProgressEvent::Backtracked;
            if (!dfsStack_.empty()) {
                updateRenderableState(nodes_[dfsStack_.back().nodeIndex]);
                return SmbDfsSearchTickResult{
                    .renderChanged = true,
                };
            }
            continue;
        }

        const size_t parentIndex = dfsFrame.nodeIndex;
        const SmbSearchLegalAction action =
            dfsFrame.actionOrdering.actions[dfsFrame.nextActionIndex++];
        const SmbSearchNode& parent = nodes_[parentIndex];
        const uint64_t parentCurrentFrontier = parent.currentFrontier;
        const uint8_t parentPlayerYScreen = parent.playerYScreen;
        const uint8_t parentVelocityStuckFrameCount = parent.velocityStuckFrameCount;

        if (!driver_->loadRuntimeSavestate(parent.savestate, 2000u)) {
            const std::string runtimeLastError = driver_->getRuntimeLastError();
            completeWithError(
                runtimeLastError.empty() ? "Failed to load SMB DFS parent savestate"
                                         : runtimeLastError);
            return SmbDfsSearchTickResult{
                .completed = true,
                .error = completionErrorMessage_,
            };
        }

        NesSuperMarioBrosEvaluator evaluator;
        if (parent.evaluatorSummary.gameState.value_or(0u) == 1u) {
            evaluator.restoreProgress(
                decodeSmbStageIndex(parent.evaluatorSummary.bestFrontier),
                decodeSmbAbsoluteX(parent.evaluatorSummary.bestFrontier),
                parent.evaluatorSummary.distanceRewardTotal,
                parent.evaluatorSummary.levelClearRewardTotal,
                parent.evaluatorSummary.gameplayFrames,
                parent.evaluatorSummary.gameplayFramesSinceProgress);
        }

        const auto stepResult = driver_->step(
            timers_, playerControlFrameToNesMask(smbSearchLegalActionToPlayerControlFrame(action)));
        if (!stepResult.runtimeHealthy || !stepResult.runtimeRunning) {
            const std::string errorMessage = stepResult.lastError.empty()
                ? "NES runtime stopped during DFS expansion"
                : stepResult.lastError;
            completeWithError(errorMessage);
            return SmbDfsSearchTickResult{
                .completed = true,
                .error = completionErrorMessage_,
            };
        }
        if (stepResult.advancedFrames == 0) {
            completeWithError("DFS expansion did not advance the NES runtime");
            return SmbDfsSearchTickResult{
                .completed = true,
                .error = completionErrorMessage_,
            };
        }
        if (!stepResult.memorySnapshot.has_value()) {
            completeWithError("DFS expansion did not provide an NES memory snapshot");
            return SmbDfsSearchTickResult{
                .completed = true,
                .error = completionErrorMessage_,
            };
        }

        NesSuperMarioBrosRamExtractor extractor;
        const NesSuperMarioBrosState state =
            extractor.extract(stepResult.memorySnapshot.value(), true);
        const std::optional<uint8_t> gameState = state.phase == SmbPhase::Gameplay
            ? std::optional<uint8_t>(1u)
            : std::optional<uint8_t>(0u);
        const auto evaluation = evaluator.evaluate(
            NesSuperMarioBrosEvaluatorInput{
                .advancedFrames = stepResult.advancedFrames,
                .state = state,
            });
        const SmbSearchEvaluatorSummary evaluatorSummary =
            buildSmbSearchEvaluatorSummary(evaluation.snapshot, gameState);

        const auto savestate = driver_->copyRuntimeSavestate();
        if (!savestate.has_value()) {
            completeWithError("Failed to capture SMB DFS child savestate");
            return SmbDfsSearchTickResult{
                .completed = true,
                .error = completionErrorMessage_,
            };
        }

        std::optional<ScenarioVideoFrame> scenarioVideoFrame = stepResult.scenarioVideoFrame;
        if (!scenarioVideoFrame.has_value()) {
            scenarioVideoFrame = driver_->copyRuntimeFrameSnapshot();
        }

        const size_t childIndex = nodes_.size();
        nodes_.push_back(
            SmbSearchNode{
                .savestate = savestate.value(),
                .memorySnapshot = stepResult.memorySnapshot.value(),
                .scenarioVideoFrame = scenarioVideoFrame,
                .evaluatorSummary = evaluatorSummary,
                .parentIndex = parentIndex,
                .actionFromParent = action,
                .currentFrontier = encodeCurrentFrontier(state),
                .gameplayFrame = evaluatorSummary.gameplayFrames,
                .playerYScreen = state.playerYScreen,
            });
        progress_.searchedNodeCount++;
        updateRenderableState(nodes_.back());
        updateBestLeaf(childIndex);

        const bool dead = evaluatorSummary.terminal || state.phase != SmbPhase::Gameplay
            || state.lifeState != SmbLifeState::Alive;
        const bool velocityStuckCandidate = options_.velocityPruningEnabled
            && state.absoluteX == decodeSmbAbsoluteX(parentCurrentFrontier) && !state.airborne
            && state.playerYScreen >= parentPlayerYScreen
            && std::abs(state.horizontalSpeedNormalized) <= kVelocityPruneHorizontalSpeedEpsilon
            && evaluatorSummary.gameplayFramesSinceProgress > 0;
        const uint8_t velocityStuckFrameCount = velocityStuckCandidate
            ? static_cast<uint8_t>(std::min<uint16_t>(
                  static_cast<uint16_t>(parentVelocityStuckFrameCount) + 1u, 255u))
            : 0u;
        nodes_.back().velocityStuckFrameCount = velocityStuckFrameCount;
        const bool velocityStuck =
            velocityStuckFrameCount >= kVelocityPruneConsecutiveFrameThreshold;
        const bool stalled =
            evaluatorSummary.gameplayFramesSinceProgress >= options_.stallFrameLimit;
        const SmbDfsSearchTraceEventType traceEvent = dead ? SmbDfsSearchTraceEventType::PrunedDead
            : velocityStuck ? SmbDfsSearchTraceEventType::PrunedVelocityStuck
            : stalled       ? SmbDfsSearchTraceEventType::PrunedStalled
                            : SmbDfsSearchTraceEventType::ExpandedAlive;
        recordTrace(
            SmbDfsSearchTraceEntry{
                .eventType = traceEvent,
                .nodeIndex = childIndex,
                .parentIndex = parentIndex,
                .action = action,
                .gameplayFrame = evaluatorSummary.gameplayFrames,
                .frontier = evaluatorSummary.bestFrontier,
                .evaluationScore = evaluatorSummary.evaluationScore,
                .framesSinceProgress = evaluatorSummary.gameplayFramesSinceProgress,
            });
        progress_.lastSearchEvent = toSearchProgressEvent(traceEvent);

        if (!dead && !velocityStuck && !stalled) {
            const SmbSearchActionOrdering actionOrdering =
                buildDfsActionOrder(state.airborne, state.verticalSpeedNormalized, action);
            dfsStack_.push_back(
                DfsFrame{
                    .nodeIndex = childIndex,
                    .nextActionIndex = 0,
                    .actionOrdering = actionOrdering,
                });
        }
        else {
            // Pruned node will never be loaded again. Release heavy data.
            releaseNodeHeavyData(childIndex);
        }

        if (options_.stopAfterBestFrontier.has_value()
            && bestFrontier_ >= options_.stopAfterBestFrontier.value()) {
            completeWithTraceEvent(SmbDfsSearchTraceEventType::CompletedMilestoneReached);
            return SmbDfsSearchTickResult{
                .completed = true,
                .frameAdvanced = true,
                .renderChanged = true,
            };
        }

        return SmbDfsSearchTickResult{
            .completed = completed_,
            .frameAdvanced = true,
            .renderChanged = true,
        };
    }
}

void SmbDfsSearch::pauseSet(bool paused)
{
    paused_ = paused;
    progress_.paused = paused_;
}

void SmbDfsSearch::stop()
{
    if (completed_) {
        return;
    }

    rebuildBestPlan();
    completed_ = true;
    paused_ = false;
    progress_.paused = false;
    progress_.lastSearchEvent = Api::SearchProgressEvent::Stopped;
    completionReason_ = SmbDfsSearchCompletionReason::Stopped;
    completionErrorMessage_.reset();

    if (bestLeafIndex_.has_value()) {
        const SmbSearchNode& bestNode = nodes_[bestLeafIndex_.value()];
        recordTrace(
            SmbDfsSearchTraceEntry{
                .eventType = SmbDfsSearchTraceEventType::Stopped,
                .nodeIndex = bestLeafIndex_.value(),
                .parentIndex = bestNode.parentIndex,
                .action = bestNode.actionFromParent,
                .gameplayFrame = bestNode.gameplayFrame,
                .frontier = bestNode.evaluatorSummary.bestFrontier,
                .evaluationScore = bestNode.evaluatorSummary.evaluationScore,
                .framesSinceProgress = bestNode.evaluatorSummary.gameplayFramesSinceProgress,
            });
        return;
    }

    recordTrace(
        SmbDfsSearchTraceEntry{
            .eventType = SmbDfsSearchTraceEventType::Stopped,
        });
}

bool SmbDfsSearch::hasPersistablePlan() const
{
    return !plan_.frames.empty();
}

bool SmbDfsSearch::hasRenderableFrame() const
{
    return scenarioVideoFrame_.has_value();
}

bool SmbDfsSearch::isCompleted() const
{
    return completed_;
}

bool SmbDfsSearch::isPaused() const
{
    return paused_;
}

std::optional<SmbDfsSearchCompletionReason> SmbDfsSearch::getCompletionReason() const
{
    return completionReason_;
}

const std::optional<std::string>& SmbDfsSearch::getCompletionErrorMessage() const
{
    return completionErrorMessage_;
}

const Api::Plan& SmbDfsSearch::getPlan() const
{
    return plan_;
}

const Api::SearchProgress& SmbDfsSearch::getProgress() const
{
    return progress_;
}

const std::optional<ScenarioVideoFrame>& SmbDfsSearch::getScenarioVideoFrame() const
{
    return scenarioVideoFrame_;
}

const std::vector<SmbDfsSearchTraceEntry>& SmbDfsSearch::getTrace() const
{
    return trace_;
}

const WorldData& SmbDfsSearch::getWorldData() const
{
    return worldData_;
}

Result<std::monostate, std::string> SmbDfsSearch::initializeRuntime()
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

    timers_ = Timers{};
    worldData_ = WorldData{};
    worldData_.width = 256;
    worldData_.height = 240;
    scenarioVideoFrame_.reset();
    plan_ = Api::Plan{};
    plan_.summary.id = UUID::generate();
    progress_ = Api::SearchProgress{};
    nodes_.clear();
    dfsStack_.clear();
    trace_.clear();
    bestLeafIndex_.reset();
    bestFrontier_ = 0;
    bestScore_ = 0.0;
    completed_ = false;
    paused_ = false;
    completionReason_.reset();
    completionErrorMessage_.reset();
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> SmbDfsSearch::initializeRootNode(
    const SmolnesRuntime::Savestate& savestate,
    const SmbSearchEvaluatorSummary& evaluatorSummary,
    const SmolnesRuntime::MemorySnapshot* memorySnapshot,
    const std::optional<ScenarioVideoFrame>& scenarioVideoFrame)
{
    nodes_.push_back(
        SmbSearchNode{
            .savestate = savestate,
            .memorySnapshot =
                memorySnapshot != nullptr ? *memorySnapshot : SmolnesRuntime::MemorySnapshot{},
            .scenarioVideoFrame = scenarioVideoFrame.has_value()
                ? scenarioVideoFrame
                : driver_->copyRuntimeFrameSnapshot(),
            .evaluatorSummary = evaluatorSummary,
            .currentFrontier = evaluatorSummary.bestFrontier,
            .gameplayFrame = evaluatorSummary.gameplayFrames,
        });
    dfsStack_.push_back(
        DfsFrame{
            .nodeIndex = 0u,
            .nextActionIndex = 0,
            .actionOrdering = buildDfsActionOrder(false, 0.0, std::nullopt),
        });

    bestLeafIndex_ = 0u;
    bestFrontier_ = evaluatorSummary.bestFrontier;
    bestScore_ = evaluatorSummary.evaluationScore;
    progress_.bestFrontier = bestFrontier_;
    updateRenderableState(nodes_.front());
    progress_.lastSearchEvent = Api::SearchProgressEvent::RootInitialized;
    rebuildBestPlan();
    recordTrace(
        SmbDfsSearchTraceEntry{
            .eventType = SmbDfsSearchTraceEventType::RootInitialized,
            .gameplayFrame = evaluatorSummary.gameplayFrames,
            .frontier = evaluatorSummary.bestFrontier,
            .evaluationScore = evaluatorSummary.evaluationScore,
            .framesSinceProgress = evaluatorSummary.gameplayFramesSinceProgress,
        });
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void SmbDfsSearch::completeWithError(const std::string& errorMessage)
{
    rebuildBestPlan();
    completed_ = true;
    paused_ = false;
    progress_.paused = false;
    progress_.lastSearchEvent = Api::SearchProgressEvent::Error;
    completionReason_ = SmbDfsSearchCompletionReason::Error;
    completionErrorMessage_ = errorMessage;

    const size_t nodeIndex = bestLeafIndex_.value_or(0u);
    const std::optional<size_t> parentIndex =
        nodeIndex < nodes_.size() ? nodes_[nodeIndex].parentIndex : std::nullopt;
    const std::optional<SmbSearchLegalAction> action =
        nodeIndex < nodes_.size() ? nodes_[nodeIndex].actionFromParent : std::nullopt;
    const uint64_t gameplayFrame = nodeIndex < nodes_.size() ? nodes_[nodeIndex].gameplayFrame : 0u;
    recordTrace(
        SmbDfsSearchTraceEntry{
            .eventType = SmbDfsSearchTraceEventType::Error,
            .nodeIndex = nodeIndex,
            .parentIndex = parentIndex,
            .action = action,
            .gameplayFrame = gameplayFrame,
            .frontier = bestFrontier_,
            .evaluationScore = bestScore_,
            .framesSinceProgress = nodeIndex < nodes_.size()
                ? nodes_[nodeIndex].evaluatorSummary.gameplayFramesSinceProgress
                : 0u,
        });
}

void SmbDfsSearch::completeWithTraceEvent(SmbDfsSearchTraceEventType eventType)
{
    rebuildBestPlan();
    completed_ = true;
    paused_ = false;
    progress_.paused = false;
    progress_.lastSearchEvent = toSearchProgressEvent(eventType);
    completionReason_ = SmbDfsSearchCompletionReason::Completed;
    completionErrorMessage_.reset();

    const size_t nodeIndex = bestLeafIndex_.value_or(0u);
    const std::optional<size_t> parentIndex =
        nodeIndex < nodes_.size() ? nodes_[nodeIndex].parentIndex : std::nullopt;
    const std::optional<SmbSearchLegalAction> action =
        nodeIndex < nodes_.size() ? nodes_[nodeIndex].actionFromParent : std::nullopt;
    const uint64_t gameplayFrame = nodeIndex < nodes_.size() ? nodes_[nodeIndex].gameplayFrame : 0u;
    recordTrace(
        SmbDfsSearchTraceEntry{
            .eventType = eventType,
            .nodeIndex = nodeIndex,
            .parentIndex = parentIndex,
            .action = action,
            .gameplayFrame = gameplayFrame,
            .frontier = bestFrontier_,
            .evaluationScore = bestScore_,
            .framesSinceProgress = nodeIndex < nodes_.size()
                ? nodes_[nodeIndex].evaluatorSummary.gameplayFramesSinceProgress
                : 0u,
        });
}

void SmbDfsSearch::rebuildBestPlan()
{
    if (!bestLeafIndex_.has_value()) {
        plan_.frames.clear();
        plan_.summary.bestFrontier = progress_.bestFrontier;
        plan_.summary.elapsedFrames = 0;
        return;
    }

    const auto planFramesResult = reconstructPlanFrames(nodes_, bestLeafIndex_.value());
    if (planFramesResult.isError()) {
        plan_.frames.clear();
        plan_.summary.bestFrontier = progress_.bestFrontier;
        plan_.summary.elapsedFrames = 0;
        return;
    }

    plan_.frames = planFramesResult.value();
    plan_.summary.bestFrontier = bestFrontier_;
    plan_.summary.elapsedFrames = plan_.frames.size();
}

void SmbDfsSearch::recordTrace(const SmbDfsSearchTraceEntry& entry)
{
    trace_.push_back(entry);
}

void SmbDfsSearch::releaseNodeHeavyData(size_t nodeIndex)
{
    if (nodeIndex >= nodes_.size()) {
        return;
    }
    SmbSearchNode& node = nodes_[nodeIndex];
    node.savestate.bytes.clear();
    node.savestate.bytes.shrink_to_fit();
    node.memorySnapshot = SmolnesRuntime::MemorySnapshot{};
    node.scenarioVideoFrame.reset();
}

void SmbDfsSearch::updateBestLeaf(size_t nodeIndex)
{
    if (nodeIndex >= nodes_.size()) {
        return;
    }

    const SmbSearchNode& candidate = nodes_[nodeIndex];
    if (!bestLeafIndex_.has_value() || candidate.evaluatorSummary.bestFrontier > bestFrontier_
        || (candidate.evaluatorSummary.bestFrontier == bestFrontier_
            && candidate.evaluatorSummary.evaluationScore > bestScore_)) {
        bestLeafIndex_ = nodeIndex;
        bestFrontier_ = candidate.evaluatorSummary.bestFrontier;
        bestScore_ = candidate.evaluatorSummary.evaluationScore;
        progress_.bestFrontier = bestFrontier_;
        rebuildBestPlan();
    }
}

void SmbDfsSearch::updateRenderableState(const SmbSearchNode& node)
{
    scenarioVideoFrame_ = node.scenarioVideoFrame;
    progress_.currentGameplayFrame = node.gameplayFrame;
    worldData_.timestep = static_cast<int32_t>(node.gameplayFrame);
    if (!scenarioVideoFrame_.has_value()) {
        return;
    }

    worldData_.width = static_cast<int16_t>(scenarioVideoFrame_->width);
    worldData_.height = static_cast<int16_t>(scenarioVideoFrame_->height);
}

} // namespace DirtSim::Server::SearchSupport
