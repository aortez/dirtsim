#pragma once

#include "ClockEventTypes.h"
#include "core/Vector2.h"
#include <unordered_map>
#include <vector>

namespace DirtSim {

class World;
struct WorldData;

// ============================================================================
// Door Manager
// ============================================================================

/**
 * Manages door openings in the world boundary walls.
 *
 * Doors are defined by their side (LEFT/RIGHT) and height along the wall.
 * Actual positions are computed from current world dimensions, ensuring
 * doors remain valid after world resize.
 *
 * When a door opens, a roof cell is placed inward to prevent material from
 * escaping. When the door closes, the wall is restored and the roof is cleared.
 */
class DoorManager {
public:
    // Create a door on the specified wall at the given height above the floor.
    // Height is relative to floor: 1 = one cell above the floor wall.
    // Returns the DoorId for future reference.
    DoorId createDoor(DoorSide side, uint32_t cells_above_floor);

    // Open/close a door by ID.
    bool openDoor(DoorId id, World& world);
    void closeDoor(DoorId id, World& world);

    // Remove a door entirely.
    void removeDoor(DoorId id);

    // Query door state.
    bool isOpen(DoorId id) const;
    bool isValidDoor(DoorId id) const;

    // Get door position (computed from current world dimensions).
    Vector2i getDoorPosition(DoorId id, const WorldData& world_data) const;
    Vector2i getRoofPosition(DoorId id, const WorldData& world_data) const;

    // Check if a position is an open door or roof cell.
    bool isOpenDoorAt(Vector2i pos, const WorldData& world_data) const;
    bool isRoofCellAt(Vector2i pos, const WorldData& world_data) const;

    // Get all open door/roof positions (for wall drawing).
    std::vector<Vector2i> getOpenDoorPositions(const WorldData& world_data) const;
    std::vector<Vector2i> getRoofPositions(const WorldData& world_data) const;

    // Close and remove all doors.
    void closeAllDoors(World& world);

private:
    // Logical door definition - positions computed from world dimensions.
    struct DoorDef {
        DoorSide side = DoorSide::LEFT;
        uint32_t cells_above_floor = 1;  // Height relative to floor (1 = one cell above floor wall).
        bool is_open = false;
    };

    std::unordered_map<DoorId, DoorDef> doors_;
    DoorId next_id_{ 1 };

    // Compute positions from door definition and world dimensions.
    Vector2i computeDoorPosition(const DoorDef& def, const WorldData& world_data) const;
    Vector2i computeRoofPosition(const DoorDef& def, const WorldData& world_data) const;
};

} // namespace DirtSim
