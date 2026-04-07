#include "server/search/SmbSearchCore.h"

#include "core/Assert.h"
#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/NesGameAdapter.h"
#include <algorithm>

namespace DirtSim::Server::SearchSupport {

bool isSmbGameplayFrame(
    const std::optional<uint8_t>& gameState, const NesGameAdapterControllerOutput& controllerOutput)
{
    return gameState.value_or(0u) == 1u
        && controllerOutput.source != NesGameAdapterControllerSource::ScriptedSetup;
}

namespace {

PlayerControlFrame makeFrame(int8_t xAxis, int8_t yAxis, uint8_t buttons)
{
    return PlayerControlFrame{
        .xAxis = xAxis,
        .yAxis = yAxis,
        .buttons = buttons,
    };
}

bool isJumpButtonAction(SmbSearchLegalAction action)
{
    switch (action) {
        case SmbSearchLegalAction::Neutral:
        case SmbSearchLegalAction::Right:
        case SmbSearchLegalAction::RightRun:
        case SmbSearchLegalAction::LeftRun:
        case SmbSearchLegalAction::Duck:
            return false;
        case SmbSearchLegalAction::RightJump:
        case SmbSearchLegalAction::RightJumpRun:
        case SmbSearchLegalAction::LeftJumpRun:
        case SmbSearchLegalAction::DuckJump:
        case SmbSearchLegalAction::DuckRightJumpRun:
        case SmbSearchLegalAction::DuckLeftJumpRun:
            return true;
    }

    return false;
}

} // namespace

SmbSearchActionOrdering buildDfsActionOrder(
    bool airborne,
    double verticalSpeedNormalized,
    std::optional<SmbSearchLegalAction> actionFromParent)
{
    const auto& defaultOrder = getSmbSearchLegalActions();
    SmbSearchActionOrdering ordering{};
    SmbSearchActionOrder& order = ordering.actions;
    size_t writePos = 0;
    const bool descendingAirborne = airborne && verticalSpeedNormalized > 0.0;

    auto isPlaced = [&](SmbSearchLegalAction action) {
        for (size_t i = 0; i < writePos; ++i) {
            if (order[i] == action) {
                return true;
            }
        }
        return false;
    };

    auto place = [&](SmbSearchLegalAction action) {
        if (descendingAirborne && isJumpButtonAction(action)) {
            return;
        }

        if (!isPlaced(action)) {
            order[writePos++] = action;
        }
    };

    // Prefer the parent's action first so DFS preserves multi-frame control patterns.
    if (actionFromParent.has_value()) {
        place(actionFromParent.value());
    }

    // While descending, A-button variants cannot recover height, so drop them entirely.
    for (const auto& action : defaultOrder) {
        place(action);
    }

    ordering.count = static_cast<uint8_t>(writePos);
    DIRTSIM_ASSERT(ordering.count > 0, "DFS action ordering unexpectedly produced no actions");
    return ordering;
}

const std::vector<SmbSearchLegalAction>& getSmbSearchLegalActions()
{
    static const std::vector<SmbSearchLegalAction> kActions = {
        SmbSearchLegalAction::RightRun,        SmbSearchLegalAction::RightJumpRun,
        SmbSearchLegalAction::Right,           SmbSearchLegalAction::RightJump,
        SmbSearchLegalAction::Neutral,         SmbSearchLegalAction::LeftRun,
        SmbSearchLegalAction::LeftJumpRun,     SmbSearchLegalAction::Duck,
        SmbSearchLegalAction::DuckJump,        SmbSearchLegalAction::DuckRightJumpRun,
        SmbSearchLegalAction::DuckLeftJumpRun,
    };
    return kActions;
}

Result<std::vector<PlayerControlFrame>, std::string> reconstructPlanFrames(
    const std::vector<SmbSearchNode>& nodes, size_t nodeIndex)
{
    if (nodeIndex >= nodes.size()) {
        return Result<std::vector<PlayerControlFrame>, std::string>::error(
            "Search node index out of range");
    }

    std::vector<PlayerControlFrame> reversedFrames;
    std::optional<size_t> cursor = nodeIndex;
    while (cursor.has_value()) {
        const SmbSearchNode& node = nodes[cursor.value()];
        if (node.actionFromParent.has_value()) {
            reversedFrames.push_back(
                smbSearchLegalActionToPlayerControlFrame(node.actionFromParent.value()));
        }

        if (!node.parentIndex.has_value()) {
            break;
        }
        if (node.parentIndex.value() >= nodes.size()) {
            return Result<std::vector<PlayerControlFrame>, std::string>::error(
                "Search node parent index out of range");
        }
        cursor = node.parentIndex;
    }

    std::reverse(reversedFrames.begin(), reversedFrames.end());
    return Result<std::vector<PlayerControlFrame>, std::string>::okay(reversedFrames);
}

SmbSearchEvaluatorSummary buildSmbSearchEvaluatorSummary(
    const NesFitnessDetails& fitnessDetails, std::optional<uint8_t> gameState)
{
    SmbSearchEvaluatorSummary summary{
        .gameState = gameState,
    };

    const auto* snapshot = std::get_if<NesSuperMarioBrosFitnessSnapshot>(&fitnessDetails);
    if (snapshot == nullptr) {
        return summary;
    }

    summary.bestFrontier = encodeSmbFrontier(*snapshot);
    summary.distanceRewardTotal = snapshot->distanceRewardTotal;
    summary.evaluationScore = snapshot->totalReward;
    summary.gameplayFrames = snapshot->gameplayFrames;
    summary.gameplayFramesSinceProgress = snapshot->framesSinceProgress;
    summary.levelClearRewardTotal = snapshot->levelClearRewardTotal;
    summary.terminal = snapshot->done;
    return summary;
}

uint16_t decodeSmbAbsoluteX(uint64_t frontier)
{
    return static_cast<uint16_t>(frontier & 0xFFFFu);
}

uint32_t decodeSmbStageIndex(uint64_t frontier)
{
    return static_cast<uint32_t>(frontier >> 16);
}

uint64_t encodeSmbFrontier(uint32_t stageIndex, uint16_t absoluteX)
{
    return (static_cast<uint64_t>(stageIndex) << 16) | static_cast<uint64_t>(absoluteX);
}

PlayerControlFrame smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction action)
{
    switch (action) {
        case SmbSearchLegalAction::Neutral:
            return makeFrame(0, 0, 0);
        case SmbSearchLegalAction::Right:
            return makeFrame(127, 0, 0);
        case SmbSearchLegalAction::RightRun:
            return makeFrame(127, 0, PlayerControlButtons::ButtonB);
        case SmbSearchLegalAction::RightJump:
            return makeFrame(127, 0, PlayerControlButtons::ButtonA);
        case SmbSearchLegalAction::RightJumpRun:
            return makeFrame(127, 0, PlayerControlButtons::ButtonA | PlayerControlButtons::ButtonB);
        case SmbSearchLegalAction::LeftRun:
            return makeFrame(-127, 0, PlayerControlButtons::ButtonB);
        case SmbSearchLegalAction::LeftJumpRun:
            return makeFrame(
                -127, 0, PlayerControlButtons::ButtonA | PlayerControlButtons::ButtonB);
        case SmbSearchLegalAction::Duck:
            return makeFrame(0, 127, 0);
        case SmbSearchLegalAction::DuckJump:
            return makeFrame(0, 127, PlayerControlButtons::ButtonA);
        case SmbSearchLegalAction::DuckRightJumpRun:
            return makeFrame(
                127, 127, PlayerControlButtons::ButtonA | PlayerControlButtons::ButtonB);
        case SmbSearchLegalAction::DuckLeftJumpRun:
            return makeFrame(
                -127, 127, PlayerControlButtons::ButtonA | PlayerControlButtons::ButtonB);
    }

    DIRTSIM_ASSERT(false, "Unhandled SmbSearchLegalAction");
    return makeFrame(0, 0, 0);
}

std::string toString(SmbSearchLegalAction action)
{
    switch (action) {
        case SmbSearchLegalAction::Neutral:
            return "Neutral";
        case SmbSearchLegalAction::Right:
            return "Right";
        case SmbSearchLegalAction::RightRun:
            return "RightRun";
        case SmbSearchLegalAction::RightJump:
            return "RightJump";
        case SmbSearchLegalAction::RightJumpRun:
            return "RightJumpRun";
        case SmbSearchLegalAction::LeftRun:
            return "LeftRun";
        case SmbSearchLegalAction::LeftJumpRun:
            return "LeftJumpRun";
        case SmbSearchLegalAction::Duck:
            return "Duck";
        case SmbSearchLegalAction::DuckJump:
            return "DuckJump";
        case SmbSearchLegalAction::DuckRightJumpRun:
            return "DuckRightJumpRun";
        case SmbSearchLegalAction::DuckLeftJumpRun:
            return "DuckLeftJumpRun";
    }

    DIRTSIM_ASSERT(false, "Unhandled SmbSearchLegalAction");
    return "Unknown";
}

uint64_t encodeSmbFrontier(const NesSuperMarioBrosFitnessSnapshot& snapshot)
{
    return encodeSmbFrontier(snapshot.bestStageIndex, snapshot.bestAbsoluteX);
}

uint8_t playerControlFrameToNesMask(const PlayerControlFrame& frame)
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

} // namespace DirtSim::Server::SearchSupport
