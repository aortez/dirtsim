#pragma once

#include "ClockEventTypes.h"
#include "core/Vector2.h"
#include <chrono>

namespace DirtSim {

class DoorManager;
class World;
struct WorldData;

namespace ClockEvents {

enum class DoorEntrySpawnStep {
    WaitingForDelay,
    ReadyToSpawn,
    SpawnComplete,
};

void initializeDoorEntrySpawn(
    DoorEntrySpawnState& state, DoorId doorId, DoorSide side, double spawnDelaySeconds);

DoorEntrySpawnStep updateDoorEntrySpawn(DoorEntrySpawnState& state, double deltaTime);

void markDoorEntrySpawnComplete(DoorEntrySpawnState& state);

Vector2i getDoorEntryPosition(
    const DoorEntrySpawnState& state, const DoorManager& doorManager, const WorldData& data);

bool closeDoorAfterActorLeaves(
    DoorEntrySpawnState& state,
    DoorManager& doorManager,
    World& world,
    Vector2i actorCell,
    std::chrono::milliseconds removalDelay);

} // namespace ClockEvents
} // namespace DirtSim
