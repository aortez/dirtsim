#pragma once

#include "core/RenderMessage.h"
#include "core/Result.h"
#include "core/input/PlayerControlFrame.h"
#include "core/scenarios/nes/NesFitnessDetails.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim::Server::SearchSupport {

enum class SmbSearchLegalAction : uint8_t {
    Neutral = 0,
    Right = 1,
    RightRun = 2,
    RightJump = 3,
    RightJumpRun = 4,
    LeftRun = 5,
    LeftJumpRun = 6,
    Duck = 7,
    DuckJump = 8,
    DuckRightJumpRun = 9,
    DuckLeftJumpRun = 10,
};

enum class SmbSearchHazardContext : uint8_t {
    Safe = 0,
    Unknown = 1,
};

enum class SmbSearchMotionContext : uint8_t {
    ControlledAirborne = 0,
    StableGrounded = 1,
    Unknown = 2,
    UnstableAirborne = 3,
};

struct SmbSearchEvaluatorSummary {
    uint64_t bestFrontier = 0;
    uint64_t gameplayFrames = 0;
    uint64_t gameplayFramesSinceProgress = 0;
    double distanceRewardTotal = 0.0;
    double evaluationScore = 0.0;
    double levelClearRewardTotal = 0.0;
    std::optional<uint8_t> gameState = std::nullopt;
    bool terminal = false;
};

struct SmbSearchNode {
    SmolnesRuntime::Savestate savestate;
    SmolnesRuntime::MemorySnapshot memorySnapshot{};
    std::optional<ScenarioVideoFrame> scenarioVideoFrame = std::nullopt;
    SmbSearchEvaluatorSummary evaluatorSummary;
    std::optional<size_t> parentIndex = std::nullopt;
    std::optional<SmbSearchLegalAction> actionFromParent = std::nullopt;
    uint64_t currentFrontier = 0;
    uint64_t gameplayFrame = 0;
    int16_t horizontalSpeed = 0;
    int16_t verticalSpeed = 0;
    SmbSearchMotionContext motionContext = SmbSearchMotionContext::Unknown;
    SmbSearchHazardContext hazardContext = SmbSearchHazardContext::Unknown;
    bool checkpointEligible = false;
};

const std::vector<SmbSearchLegalAction>& getSmbSearchLegalActions();
Result<std::vector<PlayerControlFrame>, std::string> reconstructPlanFrames(
    const std::vector<SmbSearchNode>& nodes, size_t nodeIndex);
SmbSearchEvaluatorSummary buildSmbSearchEvaluatorSummary(
    const NesFitnessDetails& fitnessDetails, std::optional<uint8_t> gameState);
uint16_t decodeSmbAbsoluteX(uint64_t frontier);
uint32_t decodeSmbStageIndex(uint64_t frontier);
uint64_t encodeSmbFrontier(uint32_t stageIndex, uint16_t absoluteX);
PlayerControlFrame smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction action);
std::string toString(SmbSearchLegalAction action);
uint64_t encodeSmbFrontier(const NesSuperMarioBrosFitnessSnapshot& snapshot);
uint8_t playerControlFrameToNesMask(const PlayerControlFrame& frame);

} // namespace DirtSim::Server::SearchSupport
