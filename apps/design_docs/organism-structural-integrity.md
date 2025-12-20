# Organism Structural Integrity Design

## The Insight

**Use existing organism infrastructure!** Trees already have:
- `organism_id` tracking which cells belong to which tree
- Seed cell (the "root" of the tree)
- TreeManager tracking all trees

**Integrate with existing rigid body system!** `World::resolveRigidBodies()` already:
- Finds connected organism structures via flood fill
- Applies unified velocity to structures

We modify it to start from seed positions and prune disconnected fragments.

## Core Algorithm: Flood-Fill + Prune + Velocity Sync

This happens inside `World::resolveRigidBodies()`, which receives TreeManager.

### For Each Tree:

1. **FLOOD FILL FROM SEED**
   ```
   Start flood-fill from tree.seed_position
   Only follow ROOT and WOOD cells (LEAF excluded - handled by bones)
   Only cells with matching organism_id
   4-connected (cardinal directions only)
   Build set of "connected" positions
   ```

2. **PRUNE DISCONNECTED FRAGMENTS**
   ```
   For all cells in tree.cells:
       If cell is ROOT or WOOD and NOT in connected set:
           cell.organism_id = 0  // Remove from organism
           Remove from tree.cells
           Remove from cell_to_tree_ map
           // Cell becomes independent particle
   ```

3. **APPLY UNIFIED VELOCITY**
   ```
   Gather pending_force from all connected cells
   acceleration = total_force / total_mass
   unified_velocity += acceleration * deltaTime
   Set all connected cells to unified_velocity
   ```
   Result: Tree moves as rigid unit, fragments fall independently

## Implementation

### Modified World::resolveRigidBodies()

```cpp
void World::resolveRigidBodies(double deltaTime)
{
    if (!tree_manager_) {
        return;
    }

    WorldData& data = getData();

    for (auto& [tree_id, tree] : tree_manager_->getTrees()) {
        // 1. Flood fill from seed to find connected structural cells.
        std::unordered_set<Vector2i> connected;
        std::queue<Vector2i> frontier;

        frontier.push(tree.seed_position);

        while (!frontier.empty()) {
            Vector2i pos = frontier.front();
            frontier.pop();

            // Bounds check.
            if (pos.x < 0 || pos.y < 0 ||
                pos.x >= (int)data.width || pos.y >= (int)data.height) {
                continue;
            }

            // Already visited.
            if (connected.count(pos)) {
                continue;
            }

            Cell& cell = data.at(pos.x, pos.y);

            // Must belong to this organism.
            if (cell.organism_id != tree_id) {
                continue;
            }

            // Only ROOT and WOOD form structural connections (LEAF excluded).
            if (cell.material_type != MaterialType::ROOT &&
                cell.material_type != MaterialType::WOOD &&
                cell.material_type != MaterialType::SEED) {
                continue;
            }

            connected.insert(pos);

            // Add 4 cardinal neighbors to frontier.
            frontier.push({pos.x - 1, pos.y});
            frontier.push({pos.x + 1, pos.y});
            frontier.push({pos.x, pos.y - 1});
            frontier.push({pos.x, pos.y + 1});
        }

        // 2. Prune disconnected ROOT/WOOD cells.
        std::vector<Vector2i> to_remove;
        for (const auto& pos : tree.cells) {
            Cell& cell = data.at(pos.x, pos.y);

            // Only prune structural materials.
            if (cell.material_type != MaterialType::ROOT &&
                cell.material_type != MaterialType::WOOD) {
                continue;
            }

            if (!connected.count(pos)) {
                cell.organism_id = 0;  // Fragment breaks off.
                to_remove.push_back(pos);
                spdlog::info("Tree {} fragment at ({},{}) disconnected",
                             tree_id, pos.x, pos.y);
            }
        }

        // Update tree's cell tracking.
        for (const auto& pos : to_remove) {
            tree.cells.erase(pos);
            // Note: cell_to_tree_ update handled by TreeManager.
        }

        // 3. Apply unified velocity to connected structure.
        if (connected.empty()) {
            continue;
        }

        // Gather forces and mass.
        Vector2d total_force;
        double total_mass = 0;

        for (const auto& pos : connected) {
            Cell& cell = data.at(pos.x, pos.y);
            total_force += cell.pending_force;
            total_mass += cell.getMass();
        }

        if (total_mass < 0.0001) {
            continue;
        }

        // F = ma -> a = F/m.
        Vector2d acceleration = total_force * (1.0 / total_mass);

        // Get current velocity from first cell (should be unified already).
        Vector2d velocity = data.at(tree.seed_position.x,
                                     tree.seed_position.y).velocity;
        velocity += acceleration * deltaTime;

        // Apply unified velocity to all connected cells.
        for (const auto& pos : connected) {
            data.at(pos.x, pos.y).velocity = velocity;
        }
    }
}
```

### Integration Point

Already in place - `resolveRigidBodies()` is called from `World::advanceTime()`:

```cpp
resolveForces(scaledDeltaTime);
resolveRigidBodies(scaledDeltaTime);  // Now handles pruning + unified velocity
processVelocityLimiting(scaledDeltaTime);
```

## Benefits

✅ **Uses existing infrastructure** - organism_id, seed tracking, TreeManager
✅ **Automatic fragmentation** - damaged trees naturally break into pieces
✅ **Simple implementation** - just flood fill + velocity averaging
✅ **Works with existing physics** - no new force calculations
✅ **Emergent behavior** - disconnected fragments fall realistically
✅ **Trees maintain shape** - horizontal branches don't sag

## Limitations

❌ **No rotation** - whole tree can't tip over as unit (yet)
❌ **No local flex** - tree is rigid (might feel stiff)
❌ **Only organisms** - doesn't help generic rigid materials
❌ **Performance** - flood fill every frame (but should be fast for small trees)

## Examples

### Example 1: Healthy Tree
```
        [SEED]
          |
        [WOOD]
          |
    [WOOD]-[WOOD]-[WOOD]-[WOOD]  ← horizontal branch

Flood fill finds all 6 cells connected to SEED
All get same velocity → branch stays horizontal
```

### Example 2: Damaged Tree
```
Before:                  After removing middle WOOD:
    [SEED]                   [SEED]
      |                        |
    [WOOD]                   [WOOD]
      |
    [WOOD]-[WOOD]                   [WOOD] ← disconnected!

Flood fill from SEED:
- Finds: SEED, 2 WOOD (connected via vertical path)
- Doesn't find: rightmost WOOD (no path to seed)
- Disconnected WOOD loses organism_id → falls as particle
```

## Implementation Status

### Completed
- ✅ Modified `World::resolveRigidBodies()` to use TreeManager
- ✅ Flood fill from seed_position (ROOT + WOOD + SEED only)
- ✅ Prune disconnected fragments (set organism_id = 0)
- ✅ Unified velocity for connected structure
- ✅ `TreeManager::removeCellsFromTree()` for pruning
- ✅ `TreeManager::addCellToTree()` for growth and testing
- ✅ Tests updated to use TreeManager properly
- ✅ New test: `DisconnectedFragmentGetsPruned`

### Next
- Convert `CantileverSupport_test` to use TreeManager (tests tree cantilever/horizontal branch stability)

### Future
- Add rotation (track angular velocity for whole structure)
- Add fracture threshold (break connections under stress)

## Design Decisions

1. **4-connected flood fill** (cardinal neighbors only, no diagonals)
2. **Every frame** - structural integrity checked each physics timestep
3. **Bones still exist** - this doesn't replace bone forces, they work together
4. **ROOT + WOOD + SEED form rigid structure** - LEAF excluded (handled by bones)
