#pragma once

#include "ClockEventTypes.h"
#include "core/Vector2.h"
#include <unordered_map>
#include <vector>

namespace DirtSim {

class World;

// ============================================================================
// Door Manager
// ============================================================================

/**
 * Manages door openings in the world boundary walls.
 *
 * Doors allow organisms to enter/exit the world. When a door opens, a roof
 * cell is placed inward to prevent material from escaping. When the door
 * closes, the wall is restored and the roof is cleared.
 */
class DoorManager {
public:
    struct DoorState {
        bool is_open = false;
        DoorSide side = DoorSide::LEFT;
        Vector2i door_pos{ -1, -1 };
        Vector2i roof_pos{ -1, -1 };
    };

    bool openDoor(Vector2i pos, DoorSide side, World& world);
    void closeDoor(Vector2i pos, World& world);
    bool isOpenDoor(Vector2i pos) const;
    bool isRoofCell(Vector2i pos) const;
    std::vector<Vector2i> getOpenDoorPositions() const;
    std::vector<Vector2i> getRoofPositions() const;
    void closeAllDoors(World& world);

private:
    std::unordered_map<Vector2i, DoorState> doors_;

    static Vector2i calculateRoofPos(Vector2i door_pos, DoorSide side);
};

} // namespace DirtSim
