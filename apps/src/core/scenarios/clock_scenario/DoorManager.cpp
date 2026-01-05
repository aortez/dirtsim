#include "DoorManager.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

Vector2i DoorManager::calculateRoofPos(Vector2i door_pos, DoorSide side)
{
    // Roof goes up one and inward one.
    // Left door: inward = +1 x. Right door: inward = -1 x.
    int dx = (side == DoorSide::LEFT) ? 1 : -1;
    return Vector2i{ door_pos.x + dx, door_pos.y - 1 };
}

bool DoorManager::openDoor(Vector2i pos, DoorSide side, World& world)
{
    // Check if already open.
    if (doors_.contains(pos) && doors_[pos].is_open) {
        return false;
    }

    DoorState state;
    state.is_open = true;
    state.side = side;
    state.door_pos = pos;
    state.roof_pos = calculateRoofPos(pos, side);

    // Clear the door cell (make it passable).
    world.getData().at(pos.x, pos.y) = Cell();

    // Place wall at roof position (displace any organisms).
    world.replaceMaterialAtCell(state.roof_pos.x, state.roof_pos.y, MaterialType::WALL);

    doors_[pos] = state;

    spdlog::info("DoorManager: Opened door at ({}, {}), roof at ({}, {})",
        pos.x, pos.y, state.roof_pos.x, state.roof_pos.y);

    return true;
}

void DoorManager::closeDoor(Vector2i pos, World& world)
{
    auto it = doors_.find(pos);
    if (it == doors_.end() || !it->second.is_open) {
        return;
    }

    const DoorState& state = it->second;

    // Restore wall at door position (push any organisms out of the way).
    world.replaceMaterialAtCell(pos.x, pos.y, MaterialType::WALL);

    // Clear roof cell (it will be restored by normal wall drawing if needed).
    Cell& roof_cell = world.getData().at(state.roof_pos.x, state.roof_pos.y);
    roof_cell = Cell();

    spdlog::info("DoorManager: Closed door at ({}, {})", pos.x, pos.y);

    doors_.erase(it);
}

bool DoorManager::isOpenDoor(Vector2i pos) const
{
    auto it = doors_.find(pos);
    return it != doors_.end() && it->second.is_open;
}

bool DoorManager::isRoofCell(Vector2i pos) const
{
    for (const auto& [door_pos, state] : doors_) {
        if (state.is_open && state.roof_pos == pos) {
            return true;
        }
    }
    return false;
}

std::vector<Vector2i> DoorManager::getOpenDoorPositions() const
{
    std::vector<Vector2i> positions;
    for (const auto& [pos, state] : doors_) {
        if (state.is_open) {
            positions.push_back(pos);
        }
    }
    return positions;
}

std::vector<Vector2i> DoorManager::getRoofPositions() const
{
    std::vector<Vector2i> positions;
    for (const auto& [pos, state] : doors_) {
        if (state.is_open) {
            positions.push_back(state.roof_pos);
        }
    }
    return positions;
}

void DoorManager::closeAllDoors(World& world)
{
    // Collect positions first to avoid iterator invalidation.
    std::vector<Vector2i> positions;
    for (const auto& [pos, state] : doors_) {
        if (state.is_open) {
            positions.push_back(pos);
        }
    }
    for (const auto& pos : positions) {
        closeDoor(pos, world);
    }
}

} // namespace DirtSim
