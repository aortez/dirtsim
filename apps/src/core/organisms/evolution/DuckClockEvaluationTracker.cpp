#include "DuckClockEvaluationTracker.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

namespace {

constexpr int kWallTouchZoneWidth = 2;

} // namespace

DuckClockEvaluationArtifacts DuckClockEvaluationTracker::buildArtifacts() const
{
    return artifacts_;
}

bool DuckClockEvaluationTracker::didJumpClearObstacle(
    const JumpAttempt& jumpAttempt, const Vector2i& landingCell, const FloorObstacle& obstacle)
{
    const int obstacleStart = obstacle.start_x;
    const int obstacleEndExclusive = obstacle.start_x + obstacle.width;
    const bool tookOffLeft = jumpAttempt.takeoffCell.x < obstacleStart;
    const bool tookOffRight = jumpAttempt.takeoffCell.x >= obstacleEndExclusive;
    const bool landedLeft = landingCell.x < obstacleStart;
    const bool landedRight = landingCell.x >= obstacleEndExclusive;
    const bool crossedObstacle = (tookOffLeft && landedRight) || (tookOffRight && landedLeft);
    if (!crossedObstacle) {
        return false;
    }

    if (jumpAttempt.minAirborneY >= jumpAttempt.takeoffCell.y) {
        return false;
    }

    return jumpAttempt.minAirborneX < obstacleEndExclusive
        && jumpAttempt.maxAirborneX >= obstacleStart;
}

void DuckClockEvaluationTracker::finalizeJumpAttempt(const Vector2i& landingCell)
{
    if (!jumpAttempt_.has_value()) {
        return;
    }

    for (const FloorObstacle& obstacle : jumpAttempt_->observedObstacles) {
        if (!didJumpClearObstacle(jumpAttempt_.value(), landingCell, obstacle)) {
            continue;
        }

        switch (obstacle.type) {
            case FloorObstacleType::HURDLE:
                ++artifacts_.hurdleClears;
                break;
            case FloorObstacleType::PIT:
                ++artifacts_.pitClears;
                break;
        }
    }

    jumpAttempt_.reset();
}

bool DuckClockEvaluationTracker::floorObstacleMatches(
    const FloorObstacle& left, const FloorObstacle& right)
{
    return left.start_x == right.start_x && left.width == right.width && left.type == right.type;
}

void DuckClockEvaluationTracker::markExitedThroughDoor(double simTime)
{
    artifacts_.bestExitDoorDistanceCells = 0.0;
    artifacts_.exitDoorDistanceObserved = true;
    artifacts_.exitedThroughDoor = true;
    artifacts_.exitDoorTime = simTime;
}

void DuckClockEvaluationTracker::recordObservedObstacles(std::span<const FloorObstacle> obstacles)
{
    if (!jumpAttempt_.has_value()) {
        return;
    }

    for (const FloorObstacle& obstacle : obstacles) {
        const auto it = std::find_if(
            jumpAttempt_->observedObstacles.begin(),
            jumpAttempt_->observedObstacles.end(),
            [&obstacle](const FloorObstacle& existing) {
                return floorObstacleMatches(existing, obstacle);
            });
        if (it == jumpAttempt_->observedObstacles.end()) {
            jumpAttempt_->observedObstacles.push_back(obstacle);
        }
    }
}

void DuckClockEvaluationTracker::reset()
{
    artifacts_ = DuckClockEvaluationArtifacts{};
    currentWallZone_ = WallZone::None;
    jumpAttempt_.reset();
    lastTouchedWallZone_ = WallZone::None;
    previousDuckAnchorCell_.reset();
    previousDuckOnGround_ = false;
    previousDuckOnGroundKnown_ = false;
}

DuckClockEvaluationTracker::WallZone DuckClockEvaluationTracker::resolveWallZone(
    int x, int worldWidth)
{
    if (x <= kWallTouchZoneWidth) {
        return WallZone::Left;
    }
    if (x >= std::max(0, worldWidth - (kWallTouchZoneWidth + 1))) {
        return WallZone::Right;
    }
    return WallZone::None;
}

void DuckClockEvaluationTracker::update(const DuckClockTrackerFrame& frame)
{
    updateTraversalState(frame);
    updateExitDoorDistance(frame);

    if (!previousDuckOnGroundKnown_) {
        previousDuckAnchorCell_ = frame.duckAnchorCell;
        previousDuckOnGround_ = frame.duckOnGround;
        previousDuckOnGroundKnown_ = true;
        return;
    }

    if (previousDuckOnGround_ && !frame.duckOnGround) {
        const Vector2i takeoffCell = previousDuckAnchorCell_.value_or(frame.duckAnchorCell);
        jumpAttempt_ = JumpAttempt{
            .takeoffCell = takeoffCell,
            .maxAirborneX = std::max(takeoffCell.x, frame.duckAnchorCell.x),
            .minAirborneX = std::min(takeoffCell.x, frame.duckAnchorCell.x),
            .minAirborneY = std::min(takeoffCell.y, frame.duckAnchorCell.y),
            .observedObstacles = {},
        };
        recordObservedObstacles(frame.obstacles);
    }
    else if (!previousDuckOnGround_ && !frame.duckOnGround && jumpAttempt_.has_value()) {
        jumpAttempt_->maxAirborneX = std::max(jumpAttempt_->maxAirborneX, frame.duckAnchorCell.x);
        jumpAttempt_->minAirborneX = std::min(jumpAttempt_->minAirborneX, frame.duckAnchorCell.x);
        jumpAttempt_->minAirborneY = std::min(jumpAttempt_->minAirborneY, frame.duckAnchorCell.y);
        recordObservedObstacles(frame.obstacles);
    }
    else if (!previousDuckOnGround_ && frame.duckOnGround) {
        if (jumpAttempt_.has_value()) {
            jumpAttempt_->maxAirborneX =
                std::max(jumpAttempt_->maxAirborneX, frame.duckAnchorCell.x);
            jumpAttempt_->minAirborneX =
                std::min(jumpAttempt_->minAirborneX, frame.duckAnchorCell.x);
            jumpAttempt_->minAirborneY =
                std::min(jumpAttempt_->minAirborneY, frame.duckAnchorCell.y);
            recordObservedObstacles(frame.obstacles);
        }
        finalizeJumpAttempt(frame.duckAnchorCell);
    }

    previousDuckAnchorCell_ = frame.duckAnchorCell;
    previousDuckOnGround_ = frame.duckOnGround;
}

void DuckClockEvaluationTracker::updateExitDoorDistance(const DuckClockTrackerFrame& frame)
{
    if (!frame.exitDoorActive || !frame.exitDoorCell.has_value()) {
        return;
    }

    const double dx = static_cast<double>(frame.duckAnchorCell.x - frame.exitDoorCell->x);
    const double dy = static_cast<double>(frame.duckAnchorCell.y - frame.exitDoorCell->y);
    const double distanceCells = std::hypot(dx, dy);
    if (!artifacts_.exitDoorDistanceObserved
        || distanceCells < artifacts_.bestExitDoorDistanceCells) {
        artifacts_.bestExitDoorDistanceCells = distanceCells;
    }
    artifacts_.exitDoorDistanceObserved = true;
}

void DuckClockEvaluationTracker::updateTraversalState(const DuckClockTrackerFrame& frame)
{
    const WallZone wallZone = resolveWallZone(frame.duckAnchorCell.x, frame.worldWidth);
    if (wallZone == currentWallZone_) {
        return;
    }

    currentWallZone_ = wallZone;
    switch (wallZone) {
        case WallZone::Left:
            ++artifacts_.leftWallTouches;
            if (lastTouchedWallZone_ == WallZone::Right) {
                ++artifacts_.fullTraversals;
            }
            lastTouchedWallZone_ = WallZone::Left;
            break;
        case WallZone::Right:
            ++artifacts_.rightWallTouches;
            if (lastTouchedWallZone_ == WallZone::Left) {
                ++artifacts_.fullTraversals;
            }
            lastTouchedWallZone_ = WallZone::Right;
            break;
        case WallZone::None:
            break;
    }
}

} // namespace DirtSim
