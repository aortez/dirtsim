#include "DoorManager.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

DoorId DoorManager::createDoor(DoorSide side, uint32_t cells_above_floor)
{
    DoorId id = next_id_++;
    doors_[id] = Door{ side, cells_above_floor, false, std::nullopt };
    return id;
}

bool DoorManager::openDoor(DoorId id, World& world)
{
    auto it = doors_.find(id);
    if (it == doors_.end()) {
        spdlog::warn("DoorManager: Cannot open invalid door {}", id);
        return false;
    }

    Door& def = it->second;
    if (def.is_open) {
        return false; // Already open.
    }

    const WorldData& data = world.getData();
    Vector2i door_pos = computeDoorPosition(def, data);
    Vector2i roof_pos = computeRoofPosition(def, data);

    // Validate positions are within bounds.
    if (!data.inBounds(door_pos.x, door_pos.y)) {
        spdlog::warn(
            "DoorManager: Door {} position ({}, {}) is outside world bounds {}x{}",
            id,
            door_pos.x,
            door_pos.y,
            data.width,
            data.height);
        return false;
    }

    if (!data.inBounds(roof_pos.x, roof_pos.y)) {
        spdlog::warn(
            "DoorManager: Door {} roof position ({}, {}) is outside world bounds {}x{}",
            id,
            roof_pos.x,
            roof_pos.y,
            data.width,
            data.height);
        return false;
    }

    // Clear the door cell (make it passable).
    world.getData().at(door_pos.x, door_pos.y) = Cell();

    // Place wall at roof position (displace any organisms).
    world.replaceMaterialAtCell(roof_pos, Material::EnumType::Wall);

    def.is_open = true;

    spdlog::info(
        "DoorManager: Opened door {} at ({}, {}), roof at ({}, {})",
        id,
        door_pos.x,
        door_pos.y,
        roof_pos.x,
        roof_pos.y);

    return true;
}

void DoorManager::closeDoor(DoorId id, World& world)
{
    auto it = doors_.find(id);
    if (it == doors_.end() || !it->second.is_open) {
        return;
    }

    Door& def = it->second;
    const WorldData& data = world.getData();
    Vector2i door_pos = computeDoorPosition(def, data);
    Vector2i roof_pos = computeRoofPosition(def, data);

    // Validate positions are within bounds before accessing.
    if (data.inBounds(door_pos.x, door_pos.y)) {
        // Restore wall at door position.
        world.replaceMaterialAtCell(door_pos, Material::EnumType::Wall);
    }

    if (data.inBounds(roof_pos.x, roof_pos.y)) {
        // Clear roof cell.
        world.getData().at(roof_pos.x, roof_pos.y) = Cell();
    }

    spdlog::info("DoorManager: Closed door {} at ({}, {})", id, door_pos.x, door_pos.y);

    def.is_open = false;
}

void DoorManager::removeDoor(DoorId id)
{
    doors_.erase(id);
}

void DoorManager::scheduleRemoval(DoorId id, std::chrono::milliseconds delay)
{
    auto it = doors_.find(id);
    if (it == doors_.end()) {
        spdlog::info("DoorManager: Door {} already removed, skipping schedule", id);
        return;
    }

    it->second.removal_time = std::chrono::steady_clock::now() + delay;
    spdlog::debug("DoorManager: Door {} scheduled for removal in {}ms", id, delay.count());
}

void DoorManager::update()
{
    auto now = std::chrono::steady_clock::now();

    // Collect doors to remove to avoid iterator invalidation.
    std::vector<DoorId> to_remove;
    for (const auto& [id, door] : doors_) {
        if (door.removal_time && now >= *door.removal_time) {
            to_remove.push_back(id);
        }
    }

    for (DoorId id : to_remove) {
        spdlog::info("DoorManager: Removing door {} (scheduled removal complete)", id);
        doors_.erase(id);
    }
}

bool DoorManager::isOpen(DoorId id) const
{
    auto it = doors_.find(id);
    return it != doors_.end() && it->second.is_open;
}

bool DoorManager::isValidDoor(DoorId id) const
{
    return doors_.contains(id);
}

Vector2i DoorManager::getDoorPosition(DoorId id, const WorldData& world_data) const
{
    auto it = doors_.find(id);
    if (it == doors_.end()) {
        return Vector2i{ -1, -1 };
    }
    return computeDoorPosition(it->second, world_data);
}

Vector2i DoorManager::getRoofPosition(DoorId id, const WorldData& world_data) const
{
    auto it = doors_.find(id);
    if (it == doors_.end()) {
        return Vector2i{ -1, -1 };
    }
    return computeRoofPosition(it->second, world_data);
}

Vector2i DoorManager::getLightPosition(DoorId id, const WorldData& world_data) const
{
    auto it = doors_.find(id);
    if (it == doors_.end()) {
        return Vector2i{ -1, -1 };
    }
    return computeLightPosition(it->second, world_data);
}

bool DoorManager::isOpenDoorAt(Vector2i pos, const WorldData& world_data) const
{
    for (const auto& [id, def] : doors_) {
        if (def.is_open && computeDoorPosition(def, world_data) == pos) {
            return true;
        }
    }
    return false;
}

bool DoorManager::isRoofCellAt(Vector2i pos, const WorldData& world_data) const
{
    for (const auto& [id, def] : doors_) {
        if (def.is_open && computeRoofPosition(def, world_data) == pos) {
            return true;
        }
    }
    return false;
}

std::vector<Vector2i> DoorManager::getOpenDoorPositions(const WorldData& world_data) const
{
    std::vector<Vector2i> positions;
    for (const auto& [id, def] : doors_) {
        if (def.is_open) {
            positions.push_back(computeDoorPosition(def, world_data));
        }
    }
    return positions;
}

std::vector<Vector2i> DoorManager::getRoofPositions(const WorldData& world_data) const
{
    std::vector<Vector2i> positions;
    for (const auto& [id, def] : doors_) {
        if (def.is_open) {
            positions.push_back(computeRoofPosition(def, world_data));
        }
    }
    return positions;
}

std::vector<Vector2i> DoorManager::getFramePositions(const WorldData& world_data) const
{
    std::vector<Vector2i> positions;
    for (const auto& [id, door] : doors_) {
        // Return frame positions for all doors (open or closed) so the
        // doorframe is visible for the door's entire lifetime.
        Vector2i door_pos = computeDoorPosition(door, world_data);

        // The door cell itself (renders as WALL instead of WOOD when closed).
        // Skip when open so the door cell remains passable.
        if (!door.is_open) {
            positions.push_back(door_pos);
        }

        // Wall cell above the door opening.
        Vector2i above_door{ door_pos.x, door_pos.y - 1 };
        if (above_door.y >= 0) {
            positions.push_back(above_door);
        }

        // Floor cell at the door position.
        int floor_y = static_cast<int>(world_data.height - 1);
        Vector2i floor_at_door{ door_pos.x, floor_y };
        positions.push_back(floor_at_door);
    }
    return positions;
}

void DoorManager::closeAllDoors(World& world)
{
    // Collect IDs first to avoid iterator invalidation.
    std::vector<DoorId> ids;
    for (const auto& [id, def] : doors_) {
        if (def.is_open) {
            ids.push_back(id);
        }
    }
    for (DoorId id : ids) {
        closeDoor(id, world);
    }
}

Vector2i DoorManager::computeDoorPosition(const Door& def, const WorldData& world_data) const
{
    // Door is on the wall edge, positioned relative to the floor.
    // Floor is at (height - 1), so door Y = floor - cells_above_floor.
    int x = (def.side == DoorSide::LEFT) ? 0 : static_cast<int>(world_data.width - 1);
    int y = static_cast<int>(world_data.height - 1 - def.cells_above_floor);
    return Vector2i{ x, y };
}

Vector2i DoorManager::computeRoofPosition(const Door& def, const WorldData& world_data) const
{
    // Roof is one cell up and one cell inward from the door.
    // Door Y is (height - 1 - cells_above_floor), so roof Y is one higher.
    int door_x = (def.side == DoorSide::LEFT) ? 0 : static_cast<int>(world_data.width - 1);
    int dx = (def.side == DoorSide::LEFT) ? 1 : -1;
    int x = door_x + dx;
    int door_y = static_cast<int>(world_data.height - 1 - def.cells_above_floor);
    int y = door_y - 1; // One cell above the door.
    return Vector2i{ x, y };
}

Vector2i DoorManager::computeLightPosition(const Door& def, const WorldData& world_data) const
{
    // Light is one cell inward from the door, at the same Y as the door.
    // This places it just inside the door opening.
    int door_x = (def.side == DoorSide::LEFT) ? 0 : static_cast<int>(world_data.width - 1);
    int dx = (def.side == DoorSide::LEFT) ? 1 : -1;
    int x = door_x + dx;
    int door_y = static_cast<int>(world_data.height - 1 - def.cells_above_floor);
    return Vector2i{ x, door_y };
}

} // namespace DirtSim
