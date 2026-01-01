# Session Notes - Organism Physics Integration

## Where We Left Off (Dec 31, 2025)

We successfully integrated rigid body organisms into the world physics system, but there's a bug preventing Goose from moving.

### What's Working ✓

- **Air resistance applied correctly** - Goose terminal velocity is ~50 cells/sec (matches Duck)
- **Force gathering works** - Goose cells accumulate gravity, air resistance from world physics
- **Basic tests pass** - Falling, standing still, collision with other organisms

### What's Broken ❌

**Goose has velocity but doesn't move:**
```
Goose walked from x=5 to x=5, distance=0 cells, max_velocity=50.0
```

**Root cause:** `collision.blocked` is always true, even when path is clear.

## Quick Start

```bash
cd /home/data/workspace/dirtsim/apps

# Build and run failing test
make build-tests
./build-debug/bin/dirtsim-tests --gtest_filter="GooseTest.GooseWalksRightWhenOnGround"

# Look for debug output:
# [brain] [debug] Goose 1: BLOCKED at predicted (X, Y), normal=(X, Y)
```

## Investigation Plan

### Theory: Self-Collision

Goose might be detecting collision with its own cell because:

1. **Timing issue:** `clearOldProjection()` runs at start of `projectToGrid()`
2. **But collision check runs BEFORE projectToGrid():**
   ```cpp
   // Goose::update() flow:
   predicted_cells = predictPosition();
   collision = detectCollisions(predicted_cells);  // ← Checks grid HERE
   if (!blocked) position = desired_position;
   projectToGrid();  // ← Clears old cells, stamps new ones
   ```

3. **Problem:** Old cells from previous frame still have `organism_id = goose_id`
4. **Detection logic** at `Organism.cpp:241` skips cells with same organism_id
5. **But predicted position might overlap with OLD position before clearing**

### Files to Check

1. **Goose.cpp:97-113** - Update loop order
   - Does `clearOldProjection()` need to run BEFORE collision check?
   - Or should predicted_cells calculation account for current position?

2. **Organism.cpp:240-244** - Self-collision logic
   ```cpp
   // Check for other organism.
   if (cell.organism_id != 0 && cell.organism_id != id_) {
       info.blocked = true;
   ```
   - Is this catching the right case?
   - What if cell has our ID but we want to move INTO that cell?

3. **Goose.cpp:85-95** - Predicted cells calculation
   ```cpp
   Vector2d world_pos{
       desired_position.x + static_cast<double>(local.local_pos.x),
       desired_position.y + static_cast<double>(local.local_pos.y)
   };
   predicted_cells.push_back(Vector2i{floor(world_pos.x), floor(world_pos.y)});
   ```
   - Is this predicting the RIGHT cells?
   - Single-cell Goose: local_pos = (0,0), so predicted = floor(desired_position)

### Debug Strategy

Add logging to understand what's happening:

```cpp
// In Goose::update() before collision check:
LOG_DEBUG(Brain, "Goose {}: current=({}, {}), desired=({:.2f}, {:.2f}), predicted=({}, {})",
    id_, getAnchorCell().x, getAnchorCell().y,
    desired_position.x, desired_position.y,
    predicted_cells[0].x, predicted_cells[0].y);

// Check what's at predicted position:
const Cell& predicted_cell = world.getData().at(predicted_cells[0].x, predicted_cells[0].y);
LOG_DEBUG(Brain, "Predicted cell: organism_id={}, material={}, fill={}",
    predicted_cell.organism_id,
    getMaterialName(predicted_cell.material_type),
    predicted_cell.fill_ratio);
```

### Likely Fix

**Option A:** Clear old projection BEFORE collision check
```cpp
void Goose::update() {
    // ...
    clearOldProjection(world);  // ← Move here

    Vector2d desired_position = ...;
    predicted_cells = ...;
    collision = detectCollisions(predicted_cells);  // Now sees empty cells

    if (!blocked) position = desired_position;
    projectToGrid(world);  // Stamp new position
}
```

**Option B:** Skip cells at current position in collision check
```cpp
// In detectCollisions() - skip if predicted == current position
for (const auto& cell_pos : target_cells) {
    if (cell_pos == current_anchor) continue;  // Allow "moving to where we are"
    // ...
}
```

Option A seems cleaner - ensures collision check sees the world state AFTER we've vacated old position.

## Key Insight

The rigid body system needs to think in continuous space but collision check operates on discrete grid. The order matters:

1. **Clear old grid stamp** - "lift piece off board"
2. **Check new position** - "can I place piece here?"
3. **Stamp new position** - "place piece on board"

Current code does: check → move → clear+stamp (wrong order!)

Should be: clear → check → move → stamp

## Architecture Notes

The physics flow is now:

```
World::advanceTime():
  1. organism_manager_->update()        // Brain/behavior (cell-based orgs do physics here)
  2. resolveForces()                    // Apply world forces to ALL cells
  3. organism_manager_->advanceTime()   // Rigid body physics (Goose)
  4. resolveRigidBodies()              // Tree flood-fill, Duck cell integration

OrganismManager::update():
  - Calls update() only on !usesRigidBodyPhysics() organisms
  - Duck, Tree run here

OrganismManager::advanceTime():
  - Calls update() only on usesRigidBodyPhysics() organisms
  - Goose runs here (after world forces applied to cells)
```

This ensures rigid body organisms can gather world physics from their cells.

## Success Criteria

When fixed, should see:
```
Goose walked from x=5 to x=35, distance=30 cells, max_velocity=50.0
```

All 8 GooseTest tests should pass.
