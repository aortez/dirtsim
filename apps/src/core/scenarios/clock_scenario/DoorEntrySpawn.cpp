#include "DoorEntrySpawn.h"
#include "DoorManager.h"
#include "core/World.h"
#include "core/WorldData.h"

namespace DirtSim {
namespace ClockEvents {

void initializeDoorEntrySpawn(
    DoorEntrySpawnState& state, DoorId doorId, DoorSide side, double spawnDelaySeconds)
{
    state.door_id = doorId;
    state.side = side;
    state.spawn_delay_seconds = spawnDelaySeconds;
    state.spawn_delay_timer = 0.0;
    state.spawn_complete = false;
    state.door_closed_after_entry = false;
}

DoorEntrySpawnStep updateDoorEntrySpawn(DoorEntrySpawnState& state, double deltaTime)
{
    if (state.spawn_complete) {
        return DoorEntrySpawnStep::SpawnComplete;
    }

    state.spawn_delay_timer += deltaTime;
    if (state.spawn_delay_timer < state.spawn_delay_seconds) {
        return DoorEntrySpawnStep::WaitingForDelay;
    }

    return DoorEntrySpawnStep::ReadyToSpawn;
}

void markDoorEntrySpawnComplete(DoorEntrySpawnState& state)
{
    state.spawn_complete = true;
}

Vector2i getDoorEntryPosition(
    const DoorEntrySpawnState& state, const DoorManager& doorManager, const WorldData& data)
{
    return doorManager.getDoorPosition(state.door_id, data);
}

bool closeDoorAfterActorLeaves(
    DoorEntrySpawnState& state,
    DoorManager& doorManager,
    World& world,
    Vector2i actorCell,
    std::chrono::milliseconds removalDelay)
{
    if (state.door_closed_after_entry) {
        return false;
    }

    if (!doorManager.isOpen(state.door_id)) {
        state.door_closed_after_entry = true;
        return false;
    }

    Vector2i entrance_pos = doorManager.getDoorPosition(state.door_id, world.getData());
    if (actorCell == entrance_pos) {
        return false;
    }

    doorManager.closeDoor(state.door_id, world);
    doorManager.scheduleRemoval(state.door_id, removalDelay);
    state.door_closed_after_entry = true;
    return true;
}

} // namespace ClockEvents
} // namespace DirtSim
