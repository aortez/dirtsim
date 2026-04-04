#include "server/search/SmbSegmentBeamSearch.h"

#include "core/Assert.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"

#include <algorithm>
#include <cmath>

namespace DirtSim::Server::SearchSupport {

namespace {

constexpr uint32_t kSmbLevelsPerWorld = 4u;
constexpr SmbSearchLegalAction kBaselineAction = SmbSearchLegalAction::RightRun;

uint64_t encodeCurrentFrontier(const NesSuperMarioBrosState& state)
{
    const uint32_t stageIndex = (static_cast<uint32_t>(state.world) * kSmbLevelsPerWorld)
        + static_cast<uint32_t>(state.level);
    return encodeSmbFrontier(stageIndex, state.absoluteX);
}

SmbSearchHazardContext classifyHazardContext(const NesSuperMarioBrosState& state)
{
    if (!state.enemyPresent) {
        return SmbSearchHazardContext::Safe;
    }
    if (std::abs(state.nearestEnemyDx) > 16 || std::abs(state.nearestEnemyDy) > 16) {
        return SmbSearchHazardContext::Safe;
    }
    return SmbSearchHazardContext::Unknown;
}

SmbSearchMotionContext classifyMotionContext(const NesSuperMarioBrosState& state)
{
    if (state.phase != SmbPhase::Gameplay || state.lifeState != SmbLifeState::Alive) {
        return SmbSearchMotionContext::Unknown;
    }
    if (!state.airborne) {
        return SmbSearchMotionContext::StableGrounded;
    }
    if (state.verticalSpeedNormalized <= 0.25 && state.horizontalSpeedNormalized >= 0.0) {
        return SmbSearchMotionContext::ControlledAirborne;
    }
    return SmbSearchMotionContext::UnstableAirborne;
}

bool isCheckpointEligible(
    const SmbSearchEvaluatorSummary& evaluatorSummary, double rootEvaluationScore)
{
    return !evaluatorSummary.terminal && evaluatorSummary.gameState.value_or(0u) == 1u
        && evaluatorSummary.evaluationScore > rootEvaluationScore;
}

Result<SmbSearchNode, std::string> makeNode(
    const SmolnesRuntime::Savestate& savestate,
    const SmbSearchEvaluatorSummary& evaluatorSummary,
    const SmolnesRuntime::MemorySnapshot& memorySnapshot,
    const std::optional<ScenarioVideoFrame>& scenarioVideoFrame,
    std::optional<size_t> parentIndex,
    std::optional<SmbSearchLegalAction> actionFromParent,
    uint64_t depth,
    double rootEvaluationScore)
{
    const NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state =
        extractor.extract(memorySnapshot, evaluatorSummary.gameState.value_or(0u) == 1u);
    const SmbSearchMotionContext motionContext = classifyMotionContext(state);
    const SmbSearchHazardContext hazardContext = classifyHazardContext(state);

    return Result<SmbSearchNode, std::string>::okay(
        SmbSearchNode{
            .savestate = savestate,
            .memorySnapshot = memorySnapshot,
            .scenarioVideoFrame = scenarioVideoFrame,
            .evaluatorSummary = evaluatorSummary,
            .parentIndex = parentIndex,
            .actionFromParent = actionFromParent,
            .currentFrontier = encodeCurrentFrontier(state),
            .depth = depth,
            .horizontalSpeed =
                static_cast<int16_t>(std::lround(state.horizontalSpeedNormalized * 100.0)),
            .verticalSpeed =
                static_cast<int16_t>(std::lround(state.verticalSpeedNormalized * 100.0)),
            .motionContext = motionContext,
            .hazardContext = hazardContext,
            .checkpointEligible = isCheckpointEligible(evaluatorSummary, rootEvaluationScore),
        });
}

bool isSuccessfulCandidateEndpoint(const SmbSearchNode& node, double rootEvaluationScore)
{
    return !node.evaluatorSummary.terminal && node.evaluatorSummary.gameState.value_or(0u) == 1u
        && node.evaluatorSummary.evaluationScore > rootEvaluationScore;
}

const std::vector<SmbSearchLegalAction>& getSmbSearchOrderedCandidateActions()
{
    static const std::vector<SmbSearchLegalAction> kActions = {
        kBaselineAction,
        SmbSearchLegalAction::RightJumpRun,
        SmbSearchLegalAction::RightJump,
        SmbSearchLegalAction::Right,
        SmbSearchLegalAction::Neutral,
        SmbSearchLegalAction::LeftRun,
        SmbSearchLegalAction::LeftJumpRun,
        SmbSearchLegalAction::Duck,
        SmbSearchLegalAction::DuckJump,
        SmbSearchLegalAction::DuckRightJumpRun,
        SmbSearchLegalAction::DuckLeftJumpRun,
    };
    return kActions;
}

uint32_t getEffectiveSearchDepth(const SmbSegmentBeamSearchParams& params)
{
    if (params.beamWidth == 0u || params.segmentFrameBudget == 0u) {
        return 0u;
    }
    return std::min(params.beamWidth, params.segmentFrameBudget);
}

uint64_t calculateTotalCandidateCount(uint32_t searchDepth)
{
    const auto& actions = getSmbSearchOrderedCandidateActions();
    uint64_t total = 1u;
    for (uint32_t depthIndex = 0u; depthIndex < searchDepth; ++depthIndex) {
        total *= actions.size();
    }
    return total;
}

std::vector<uint32_t> buildSlotStartFrames(uint32_t searchDepth, uint32_t segmentFrameBudget)
{
    std::vector<uint32_t> startFrames(searchDepth, 0u);
    uint32_t nextFrame = 0u;
    const uint32_t baseFramesPerSlot = segmentFrameBudget / searchDepth;
    const uint32_t remainderFrames = segmentFrameBudget % searchDepth;
    for (uint32_t slotIndex = 0u; slotIndex < searchDepth; ++slotIndex) {
        startFrames[slotIndex] = nextFrame;
        nextFrame += baseFramesPerSlot + (slotIndex < remainderFrames ? 1u : 0u);
    }
    return startFrames;
}

std::vector<std::vector<SmbSearchLegalAction>> buildOrderedCandidateSlotActions(
    uint32_t searchDepth)
{
    if (searchDepth == 0u) {
        return {};
    }

    const auto& orderedActions = getSmbSearchOrderedCandidateActions();
    std::vector<std::vector<SmbSearchLegalAction>> candidates;
    std::vector<SmbSearchLegalAction> current(searchDepth, kBaselineAction);

    const auto buildCandidates = [&](const auto& self, uint32_t slotIndex) -> void {
        if (slotIndex == searchDepth) {
            candidates.push_back(current);
            return;
        }

        for (const auto action : orderedActions) {
            current[slotIndex] = action;
            self(self, slotIndex + 1u);
        }
    };
    buildCandidates(buildCandidates, 0u);

    const auto actionRank = [&](SmbSearchLegalAction action) -> size_t {
        const auto actionIt = std::find(orderedActions.begin(), orderedActions.end(), action);
        DIRTSIM_ASSERT(actionIt != orderedActions.end(), "Missing SMB search action rank");
        return static_cast<size_t>(std::distance(orderedActions.begin(), actionIt));
    };

    std::stable_sort(
        candidates.begin(), candidates.end(), [&](const auto& left, const auto& right) {
            const auto countDeviations = [](const auto& candidate) {
                return static_cast<size_t>(
                    std::count_if(candidate.begin(), candidate.end(), [](const auto action) {
                        return action != kBaselineAction;
                    }));
            };

            const size_t leftDeviationCount = countDeviations(left);
            const size_t rightDeviationCount = countDeviations(right);
            if (leftDeviationCount != rightDeviationCount) {
                return leftDeviationCount < rightDeviationCount;
            }
            for (size_t actionIndex = 0u; actionIndex < left.size(); ++actionIndex) {
                const size_t leftRank = actionRank(left[actionIndex]);
                const size_t rightRank = actionRank(right[actionIndex]);
                if (leftRank != rightRank) {
                    return leftRank < rightRank;
                }
            }
            return false;
        });

    return candidates;
}

std::vector<SmbSearchLegalAction> buildCandidateFrameActions(
    uint64_t candidateIndex, uint32_t searchDepth, uint32_t segmentFrameBudget)
{
    std::vector<SmbSearchLegalAction> frameActions(segmentFrameBudget, kBaselineAction);
    if (segmentFrameBudget == 0u || searchDepth == 0u) {
        return frameActions;
    }

    const auto slotCandidates = buildOrderedCandidateSlotActions(searchDepth);
    DIRTSIM_ASSERT(candidateIndex < slotCandidates.size(), "SMB candidate index out of range");
    const auto slotStartFrames = buildSlotStartFrames(searchDepth, segmentFrameBudget);
    const auto& slotActions = slotCandidates[static_cast<size_t>(candidateIndex)];
    for (uint32_t slotIndex = 0u; slotIndex < searchDepth; ++slotIndex) {
        const uint32_t startFrame = slotStartFrames[slotIndex];
        const uint32_t endFrame =
            slotIndex + 1u < searchDepth ? slotStartFrames[slotIndex + 1u] : segmentFrameBudget;
        std::fill(
            frameActions.begin() + static_cast<std::ptrdiff_t>(startFrame),
            frameActions.begin() + static_cast<std::ptrdiff_t>(endFrame),
            slotActions[slotIndex]);
    }
    return frameActions;
}

std::vector<PlayerControlFrame> buildCandidateFrames(
    const std::vector<SmbSearchLegalAction>& actions)
{
    std::vector<PlayerControlFrame> frames;
    frames.reserve(actions.size());
    for (const auto action : actions) {
        frames.push_back(smbSearchLegalActionToPlayerControlFrame(action));
    }
    return frames;
}

Result<size_t, std::string> appendCandidateEndpointNodes(
    SmbSegmentBeamSearchState& state,
    const std::vector<SmbSearchLegalAction>& candidateActions,
    const SmbSearchReplayResult& replay)
{
    if (!replay.savestate.has_value()) {
        return Result<size_t, std::string>::error("Candidate replay is missing a savestate");
    }
    if (!replay.memorySnapshot.has_value()) {
        return Result<size_t, std::string>::error("Candidate replay is missing a memory snapshot");
    }
    if (state.nodes.empty()) {
        return Result<size_t, std::string>::error("Candidate search state is missing a root node");
    }

    size_t parentIndex = 0u;
    for (size_t frameIndex = 0u; frameIndex < candidateActions.size(); ++frameIndex) {
        const bool isLastFrame = frameIndex + 1u == candidateActions.size();
        SmbSearchNode node{};
        node.parentIndex = parentIndex;
        node.actionFromParent = candidateActions[frameIndex];
        node.depth = state.nodes[parentIndex].depth + 1u;

        if (isLastFrame) {
            const auto endpointNodeResult = makeNode(
                replay.savestate.value(),
                replay.evaluatorSummary,
                replay.memorySnapshot.value(),
                replay.scenarioVideoFrame,
                parentIndex,
                candidateActions[frameIndex],
                node.depth,
                state.rootEvaluationScore);
            if (endpointNodeResult.isError()) {
                return Result<size_t, std::string>::error(endpointNodeResult.errorValue());
            }
            node = endpointNodeResult.value();
        }

        state.nodes.push_back(node);
        parentIndex = state.nodes.size() - 1u;
    }

    return Result<size_t, std::string>::okay(parentIndex);
}

} // namespace

SmbSegmentBeamSearchResult SmbSegmentBeamSearch::buildResult(
    const SmbSegmentBeamSearchState& state) const
{
    return SmbSegmentBeamSearchResult{
        .bestNodeIndex = state.bestNodeIndex,
        .frontierNodeIndices = state.frontierNodeIndices,
        .nodes = state.nodes,
        .expandedNodeCount = state.expandedNodeCount,
        .nextCandidateIndex = state.nextCandidateIndex,
        .rootFrontier = state.rootFrontier,
        .totalCandidateCount = state.totalCandidateCount,
        .rootEvaluationScore = state.rootEvaluationScore,
        .searchDepth = state.searchDepth,
        .frontierImproved = state.frontierImproved,
    };
}

Result<SmbSegmentBeamSearchState, std::string> SmbSegmentBeamSearch::start(
    const SmbSearchRootFixture& rootFixture, uint64_t startCandidateIndex) const
{
    if (rootFixture.evaluatorSummary.gameState.value_or(0u) != 1u) {
        return Result<SmbSegmentBeamSearchState, std::string>::error(
            "SMB segment search requires a gameplay root");
    }
    if (!rootFixture.memorySnapshot.has_value()) {
        return Result<SmbSegmentBeamSearchState, std::string>::error(
            "SMB segment search root is missing a memory snapshot");
    }

    SmbSegmentBeamSearchState state{};
    state.nextCandidateIndex = startCandidateIndex;
    state.rootFrontier = rootFixture.evaluatorSummary.bestFrontier;
    state.rootEvaluationScore = rootFixture.evaluatorSummary.evaluationScore;

    const auto rootNodeResult = makeNode(
        rootFixture.savestate,
        rootFixture.evaluatorSummary,
        rootFixture.memorySnapshot.value(),
        rootFixture.scenarioVideoFrame,
        std::nullopt,
        std::nullopt,
        0u,
        state.rootEvaluationScore);
    if (rootNodeResult.isError()) {
        return Result<SmbSegmentBeamSearchState, std::string>::error(
            "Failed to build SMB root search node: " + rootNodeResult.errorValue());
    }

    state.nodes.push_back(rootNodeResult.value());
    state.frontierNodeIndices.push_back(0u);
    return Result<SmbSegmentBeamSearchState, std::string>::okay(std::move(state));
}

Result<SmbSegmentBeamSearchTickResult, std::string> SmbSegmentBeamSearch::tick(
    SmbSegmentBeamSearchState& state, const SmbSegmentBeamSearchParams& params) const
{
    if (params.beamWidth == 0u) {
        return Result<SmbSegmentBeamSearchTickResult, std::string>::error(
            "SMB segment search requires search depth > 0");
    }
    if (state.completed) {
        return Result<SmbSegmentBeamSearchTickResult, std::string>::okay(
            SmbSegmentBeamSearchTickResult{
                .completed = true,
            });
    }

    const uint32_t effectiveDepth = getEffectiveSearchDepth(params);
    if (effectiveDepth == 0u) {
        state.completed = true;
        return Result<SmbSegmentBeamSearchTickResult, std::string>::okay(
            SmbSegmentBeamSearchTickResult{
                .completed = true,
            });
    }
    if (state.searchDepth == 0u) {
        state.searchDepth = effectiveDepth;
        state.totalCandidateCount = calculateTotalCandidateCount(state.searchDepth);
    }
    if (state.searchDepth != effectiveDepth) {
        return Result<SmbSegmentBeamSearchTickResult, std::string>::error(
            "SMB segment search depth changed during execution");
    }
    if (state.nextCandidateIndex >= state.totalCandidateCount) {
        state.completed = true;
        return Result<SmbSegmentBeamSearchTickResult, std::string>::okay(
            SmbSegmentBeamSearchTickResult{
                .completed = true,
            });
    }
    if (state.nodes.empty()) {
        return Result<SmbSegmentBeamSearchTickResult, std::string>::error(
            "SMB segment search state is missing its root node");
    }

    const std::vector<SmbSearchLegalAction> candidateActions = buildCandidateFrameActions(
        state.nextCandidateIndex, state.searchDepth, params.segmentFrameBudget);
    const std::vector<PlayerControlFrame> candidateFrames = buildCandidateFrames(candidateActions);
    if (candidateFrames.size() != params.segmentFrameBudget) {
        return Result<SmbSegmentBeamSearchTickResult, std::string>::error(
            "SMB segment candidate frames did not match segment budget");
    }

    const SmbSearchHarness harness;
    const SmbSearchNode& rootNode = state.nodes.front();
    const auto replayResult =
        harness.replayFromRoot(rootNode.savestate, rootNode.evaluatorSummary, candidateFrames);
    if (replayResult.isError()) {
        return Result<SmbSegmentBeamSearchTickResult, std::string>::error(
            "Failed to replay SMB segment candidate " + std::to_string(state.nextCandidateIndex)
            + ": " + replayResult.errorValue());
    }

    state.expandedNodeCount += 1u;
    state.completedSteps = params.segmentFrameBudget;

    const auto endpointIndexResult =
        appendCandidateEndpointNodes(state, candidateActions, replayResult.value());
    if (endpointIndexResult.isError()) {
        return Result<SmbSegmentBeamSearchTickResult, std::string>::error(
            "Failed to append SMB segment candidate endpoint: " + endpointIndexResult.errorValue());
    }

    const size_t endpointIndex = endpointIndexResult.value();
    state.frontierNodeIndices = { endpointIndex };
    const SmbSearchNode& endpointNode = state.nodes[endpointIndex];
    if (endpointNode.evaluatorSummary.bestFrontier > state.rootFrontier) {
        state.frontierImproved = true;
    }

    state.nextCandidateIndex += 1u;
    if (isSuccessfulCandidateEndpoint(endpointNode, state.rootEvaluationScore)) {
        state.bestNodeIndex = endpointIndex;
        state.completed = true;
    }

    if (state.nextCandidateIndex >= state.totalCandidateCount) {
        state.completed = true;
    }

    return Result<SmbSegmentBeamSearchTickResult, std::string>::okay(
        SmbSegmentBeamSearchTickResult{
            .completed = state.completed,
            .stepAdvanced = true,
        });
}

Result<SmbSegmentBeamSearchResult, std::string> SmbSegmentBeamSearch::run(
    const SmbSearchRootFixture& rootFixture, const SmbSegmentBeamSearchParams& params) const
{
    auto stateResult = start(rootFixture);
    if (stateResult.isError()) {
        return Result<SmbSegmentBeamSearchResult, std::string>::error(stateResult.errorValue());
    }

    SmbSegmentBeamSearchState state = std::move(stateResult).value();
    while (!state.completed) {
        const auto tickResult = tick(state, params);
        if (tickResult.isError()) {
            return Result<SmbSegmentBeamSearchResult, std::string>::error(tickResult.errorValue());
        }
    }

    return Result<SmbSegmentBeamSearchResult, std::string>::okay(buildResult(state));
}

} // namespace DirtSim::Server::SearchSupport
