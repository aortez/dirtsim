#pragma once

#include "FitnessCalculator.h"
#include "core/Vector2.h"
#include "core/scenarios/clock_scenario/ObstacleManager.h"

#include <optional>
#include <span>
#include <vector>

namespace DirtSim {

struct DuckClockTrackerFrame {
    double simTime = 0.0;
    int worldWidth = 0;
    Vector2i duckAnchorCell{ 0, 0 };
    bool duckOnGround = false;
    bool exitDoorActive = false;
    std::optional<Vector2i> exitDoorCell = std::nullopt;
    std::span<const FloorObstacle> obstacles{};
};

class DuckClockEvaluationTracker {
public:
    DuckClockEvaluationArtifacts buildArtifacts() const;
    void markExitedThroughDoor(double simTime);
    void reset();
    void update(const DuckClockTrackerFrame& frame);

private:
    struct JumpAttempt {
        Vector2i takeoffCell{ 0, 0 };
        int maxAirborneX = 0;
        int minAirborneX = 0;
        int minAirborneY = 0;
        std::vector<FloorObstacle> observedObstacles;
    };

    enum class WallZone { None, Left, Right };

    static bool didJumpClearObstacle(
        const JumpAttempt& jumpAttempt, const Vector2i& landingCell, const FloorObstacle& obstacle);
    static bool floorObstacleMatches(const FloorObstacle& left, const FloorObstacle& right);
    static double resolveTraversalProgress(int x, int worldWidth, WallZone lastTouchedWallZone);
    static WallZone resolveWallZone(int x, int worldWidth);

    void cleanupInactiveObstacleOpportunities(std::span<const FloorObstacle> obstacles);
    void updateObstacleOpportunities(const DuckClockTrackerFrame& frame);
    void finalizeJumpAttempt(const Vector2i& landingCell);
    void recordObservedObstacles(std::span<const FloorObstacle> obstacles);
    void updateExitDoorDistance(const DuckClockTrackerFrame& frame);
    void updateTraversalProgress(const DuckClockTrackerFrame& frame);
    void updateTraversalState(const DuckClockTrackerFrame& frame);

    DuckClockEvaluationArtifacts artifacts_{};
    std::vector<FloorObstacle> activeClearedObstacles_;
    std::vector<FloorObstacle> activeOpportunityObstacles_;
    double currentTraversalProgress_ = 0.0;
    std::optional<JumpAttempt> jumpAttempt_ = std::nullopt;
    std::optional<Vector2i> previousDuckAnchorCell_ = std::nullopt;
    WallZone currentWallZone_ = WallZone::None;
    WallZone lastTouchedWallZone_ = WallZone::None;
    bool previousDuckOnGround_ = false;
    bool previousDuckOnGroundKnown_ = false;
};

} // namespace DirtSim
