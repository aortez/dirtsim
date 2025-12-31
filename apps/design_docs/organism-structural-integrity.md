# Organism Structural Integrity Design

## Overview

Organisms (trees, future creatures) are represented as **rigid bodies in continuous space** that project onto the discrete cell grid each frame. This approach treats organisms as proper physics objects rather than collections of cells with synchronized velocities.

## Problem with Previous Approach

The original design used **velocity synchronization**: all cells in an organism received the same velocity via flood-fill. This had a fundamental flaw:

**Cells have independent COMs (Centers of Mass)** that move based on velocity. Even with unified velocity, cells cross grid boundaries at different times because their COMs start at different positions within their cells. This causes the structure to tear apart during movement.

```
Organism moving right at velocity (0.5, 0):

[WOOD A]─[WOOD B]─[WOOD C]
com.x=0.8  com.x=0.2  com.x=-0.3

Frame 1: Cell A crosses boundary first (com reaches 1.0)
Frame 2: Cell B still inside its cell
Frame 3: Cell B crosses boundary
→ Structure tears apart!
```

The velocity sync approach is preserved below for historical reference but is **obsolete**.

---

## New Design: Organisms as Continuous-Space Rigid Bodies

### Core Concept

**Two representations:**
1. **Physics**: Organism lives in continuous space (position, velocity as Vector2d)
2. **Rendering**: Organism projects onto discrete grid each frame

```
Physics (continuous) → Collision Detection → Response → Grid Projection
         ↑                                                      ↓
         └──────────── Forces Gathered ─────────────────────────┘
```

### Phase 1: Position + Velocity (No Rotation)

#### Organism Structure

```cpp
struct Organism {
    OrganismId id;
    OrganismType type;  // TREE, DUCK, etc.

    // Continuous rigid body state
    Vector2d position;        // Anchor point (seed/root location)
    Vector2d velocity;        // Linear velocity (cells/sec)
    double mass;              // Total mass (computed from shape)
    Vector2d center_of_mass;  // Relative to position (computed)

    // Shape definition (local coordinates, axis-aligned)
    struct LocalCell {
        Vector2i local_pos;      // Position relative to anchor
        MaterialType material;
        double fill_ratio;
    };
    std::vector<LocalCell> local_shape;

    // Current grid projection (updated each frame)
    std::vector<Vector2i> occupied_cells;
};
```

#### Update Loop (Each Frame)

```cpp
void updateOrganism(Organism& org, Grid& grid, double dt) {
    // 1. Gather forces from environment
    Vector2d total_force = gatherEnvironmentForces(org, grid);

    // 2. Compute desired movement
    Vector2d acceleration = total_force / org.mass;
    Vector2d desired_velocity = org.velocity + acceleration * dt;
    Vector2d desired_position = org.position + desired_velocity * dt;

    // 3. Predict where cells would be
    std::vector<Vector2i> predicted_cells;
    for (const auto& local : org.local_shape) {
        Vector2d world = desired_position + Vector2d(local.local_pos);
        predicted_cells.push_back(snap(world));  // floor()
    }

    // 4. Detect collisions
    CollisionInfo collision = detectCollisions(predicted_cells, grid, org.id);

    // 5. Collision response (impulse-based)
    if (collision.blocked) {
        handleCollision_Impulse(org, collision, grid, dt);
    } else {
        org.position = desired_position;
        org.velocity = desired_velocity;
    }

    // 6. Project organism onto grid
    projectToGrid(org, grid);
}
```

### Collision Detection

```cpp
struct CollisionInfo {
    bool blocked;
    std::vector<Vector2i> blocked_cells;
    Vector2d contact_normal;  // Average surface normal
};

CollisionInfo detectCollisions(
    const std::vector<Vector2i>& target_cells,
    const Grid& grid,
    OrganismId org_id)
{
    CollisionInfo info{.blocked = false};
    Vector2d normal_sum = {0, 0};

    for (const auto& cell_pos : target_cells) {
        if (!isInBounds(cell_pos, grid)) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeBoundaryNormal(cell_pos);
            continue;
        }

        const Cell& cell = grid.at(cell_pos);

        // Blocked by wall
        if (cell.material_type == MaterialType::WALL) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeContactNormal(cell_pos, grid);
        }
        // Blocked by another organism
        else if (cell.organism_id != 0 && cell.organism_id != org_id) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeContactNormal(cell_pos, grid);
        }
        // Blocked by dense solid material
        else if (isSolidMaterial(cell) && cell.fill_ratio > 0.8) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeContactNormal(cell_pos, grid);
        }
    }

    if (info.blocked) {
        info.contact_normal = normal_sum.normalize();
    }

    return info;
}
```

### Collision Response (Impulse-Based)

Standard rigid body impulse calculation, same method used in Box2D/PhysX/Bullet:

```cpp
void handleCollision_Impulse(
    Organism& org,
    const CollisionInfo& collision,
    Grid& grid,
    double dt)
{
    Vector2d normal = collision.contact_normal;
    double v_normal = dot(org.velocity, normal);

    if (v_normal >= 0) return;  // Moving away from surface

    // Coefficient of restitution
    // 0.0 = perfectly inelastic (no bounce)
    // 1.0 = perfectly elastic (full bounce)
    double e = 0.3;

    // Impulse magnitude: j = -(1 + e) * v_n * m
    double j = -(1 + e) * v_normal * org.mass;
    Vector2d impulse = normal * j;

    // Apply to organism
    org.velocity += impulse / org.mass;

    // Apply reaction forces to environment (Newton's 3rd law)
    int num_contacts = collision.blocked_cells.size();
    Vector2d force_per_cell = -impulse / (dt * num_contacts);

    for (const auto& cell_pos : collision.blocked_cells) {
        Cell& cell = grid.at(cell_pos);

        if (cell.material_type == MaterialType::WALL) {
            continue;  // Walls are immovable
        }

        // Apply force uniformly - material properties handle response
        cell.pending_force += force_per_cell;
    }
}
```

### Reaction Forces (Newton's 3rd Law)

Organisms apply forces back to the environment. No material-specific heuristics - the existing physics system handles material differences through viscosity, friction, and pressure.

```cpp
void applyWeightToGround(Organism& org, Grid& grid) {
    double weight = org.mass * gravity;
    std::vector<Vector2i> bottom_cells = findGroundContactCells(org);

    if (bottom_cells.empty()) return;

    Vector2d force_per_cell = {0, weight / bottom_cells.size()};

    for (const auto& cell_pos : bottom_cells) {
        Vector2i ground_pos = cell_pos + Vector2i{0, 1};
        if (!isInBounds(ground_pos)) continue;

        Cell& ground_cell = grid.at(ground_pos);
        if (ground_cell.organism_id == org.id) continue;
        if (ground_cell.material_type == MaterialType::WALL) continue;

        ground_cell.pending_force += force_per_cell;
    }
}
```

### Grid Projection

```cpp
void projectToGrid(Organism& org, Grid& grid) {
    // Clear old projections
    for (const auto& old_pos : org.occupied_cells) {
        Cell& cell = grid.at(old_pos);
        if (cell.organism_id == org.id) {
            cell.organism_id = 0;
            cell.material_type = MaterialType::AIR;
            cell.fill_ratio = 0.0;
        }
    }

    org.occupied_cells.clear();

    // Project each local cell to grid
    for (const auto& local : org.local_shape) {
        Vector2d world_pos = org.position + Vector2d(local.local_pos);
        Vector2i grid_pos = snap(world_pos);  // floor()

        if (!isInBounds(grid_pos, grid)) continue;

        Cell& cell = grid.at(grid_pos);
        cell.organism_id = org.id;
        cell.material_type = local.material;
        cell.fill_ratio = local.fill_ratio;
        cell.velocity = org.velocity;

        // Compute sub-cell COM from fractional position
        Vector2d offset = world_pos - Vector2d(grid_pos);
        cell.com = offset * 2.0 - Vector2d{1, 1};  // Map [0,1] → [-1,1]

        org.occupied_cells.push_back(grid_pos);
    }
}
```

### Force Gathering

```cpp
Vector2d gatherEnvironmentForces(const Organism& org, const Grid& grid) {
    Vector2d total_force = {0, 0};

    for (const auto& local : org.local_shape) {
        Vector2d world_pos = org.position + Vector2d(local.local_pos);
        Vector2i grid_pos = snap(world_pos);

        if (!isInBounds(grid_pos, grid)) continue;

        total_force += grid.at(grid_pos).pending_force;
    }

    // Gravity
    total_force += org.mass * gravity_vector;

    return total_force;
}
```

### Growth

```cpp
void growCell(Organism& org, Vector2i local_pos, MaterialType mat) {
    org.local_shape.push_back({
        .local_pos = local_pos,
        .material = mat,
        .fill_ratio = 1.0
    });

    double new_mass = getMaterialProperties(mat).density;
    org.mass += new_mass;
    org.center_of_mass = recomputeCOM(org.local_shape);
    // Position (anchor) stays fixed, COM shifts relative to it
}
```

## Design Decisions

1. **Position = anchor point (not COM)**: Keeps coordinates stable during growth.
2. **No rotation in Phase 1**: Axis-aligned organisms only. Add rotation later.
3. **Impulse-based collision**: Industry standard, physically correct.
4. **No connectivity checks**: Removed for now. Can add back for fragmentation.
5. **Simple force gathering**: floor() for grid lookup, sufficient for phase 1.
6. **Uniform force application**: No material-specific heuristics.
7. **Grid-only serialization**: Client sees projected grid, unchanged.

## Phase 2: Future Enhancements

- **Rotation**: Add `rotation`, `angular_velocity`, `moment_of_inertia`.
- **Torque**: Compute from force positions relative to COM.
- **Stress-based fracture**: Track stress at bonds, break under load.
- **Organism-organism collision**: Two-body impulse resolution.

## Test Plan

### Existing Tests - What To Do

**Tests to Adapt** (in `src/tests/RigidBodyIntegration_test.cpp`):

| Test | Current Behavior | New Behavior |
|------|------------------|--------------|
| `FloatingStructureFallsTogether` | Checks cells have unified velocity | Check organism.position falls, all projected cells move together |
| `TreeStructureMovesAsUnit` | Checks seed/wood have same velocity | Check organism has single velocity, grid projection correct |
| `MultipleStructuresMoveIndependently` | Checks two trees fall separately | Same concept, but check organism.position for each |

**Tests to Remove**:

| Test | Reason |
|------|--------|
| `DisconnectedFragmentGetsPruned` | We removed connectivity checks in Phase 1 |
| All tests in `WorldRigidBodyCalculator_test.cpp` | Calculator class will be removed/replaced |

**Tests to Keep Unchanged**:

| Test File | Reason |
|-----------|--------|
| `TreeManager_test.cpp` | Tree creation/tracking still relevant |
| `Duck_test.cpp` | Ducks are single-cell, work as-is |
| `TreeSensory_test.cpp` | Sensory perception orthogonal to physics |
| `OrganismSensoryData_test.cpp` | Sensory data orthogonal to physics |
| `TreeGermination_test.cpp` (most tests) | Growth logic still relevant |

**Failing Test to Fix**:

| Test | Current Issue | Expected After New System |
|------|---------------|---------------------------|
| `TreeGerminationTest.ExtendedGrowthStability` | COM drift during movement | Should pass - organism position is continuous, no COM drift |

### New Tests to Write

**1. Organism Physics Tests** (`OrganismPhysics_test.cpp`):

```cpp
// Basic physics integration
TEST(OrganismPhysicsTest, PositionUpdatesWithVelocity)
// organism.position += organism.velocity * dt

TEST(OrganismPhysicsTest, VelocityUpdatesWithForce)
// organism.velocity += (total_force / organism.mass) * dt

TEST(OrganismPhysicsTest, MassComputedFromLocalShape)
// organism.mass = sum of material densities in local_shape

TEST(OrganismPhysicsTest, COMComputedFromLocalShape)
// organism.center_of_mass = weighted average of local cell positions

TEST(OrganismPhysicsTest, GravityAcceleratesOrganism)
// Unsupported organism accelerates downward
```

**2. Collision Detection Tests** (`OrganismCollision_test.cpp`):

```cpp
TEST(OrganismCollisionTest, DetectsWallCollision)
// Target cell with WALL material → collision.blocked = true

TEST(OrganismCollisionTest, DetectsOtherOrganismCollision)
// Target cell with different organism_id → blocked

TEST(OrganismCollisionTest, DetectsDenseSolidCollision)
// Target cell with fill_ratio > 0.8 and solid material → blocked

TEST(OrganismCollisionTest, NoCollisionWithEmptySpace)
// Target cell is AIR → not blocked

TEST(OrganismCollisionTest, NoCollisionWithOwnCells)
// Target cell has same organism_id → not blocked

TEST(OrganismCollisionTest, ContactNormalPointsAwayFromObstacle)
// Normal computed correctly for collision response

TEST(OrganismCollisionTest, DetectsWorldBoundaryCollision)
// Target cell outside grid bounds → blocked
```

**3. Collision Response Tests** (`OrganismCollisionResponse_test.cpp`):

```cpp
TEST(OrganismCollisionResponseTest, ImpulseReversesNormalVelocity)
// Velocity component into surface is reversed

TEST(OrganismCollisionResponseTest, RestitutionZeroStopsMovement)
// e=0 → velocity normal component becomes zero

TEST(OrganismCollisionResponseTest, RestitutionOneFullBounce)
// e=1 → velocity normal component fully reversed

TEST(OrganismCollisionResponseTest, TangentialVelocityPreserved)
// Velocity parallel to surface unchanged (before friction)

TEST(OrganismCollisionResponseTest, ReactionForceAppliedToEnvironment)
// Blocked cells receive equal-and-opposite force
```

**4. Grid Projection Tests** (`OrganismGridProjection_test.cpp`):

```cpp
TEST(OrganismGridProjectionTest, ClearsOldOccupiedCells)
// Previous projection cleared before new one

TEST(OrganismGridProjectionTest, ProjectsLocalShapeToGrid)
// Each local cell appears at correct grid position

TEST(OrganismGridProjectionTest, SetsOrganismIdOnProjectedCells)
// Grid cells have correct organism_id

TEST(OrganismGridProjectionTest, SetsMaterialTypeOnProjectedCells)
// Grid cells have correct material from local_shape

TEST(OrganismGridProjectionTest, SetsVelocityOnProjectedCells)
// Grid cells have organism's velocity

TEST(OrganismGridProjectionTest, ComputesSubCellCOMFromFractionalPosition)
// cell.com reflects organism's sub-grid position
```

**5. Force Gathering Tests** (`OrganismForceGathering_test.cpp`):

```cpp
TEST(OrganismForceGatheringTest, GathersPendingForceFromOccupiedCells)
// Sum of pending_force from all cells organism occupies

TEST(OrganismForceGatheringTest, IncludesGravity)
// Total force includes organism.mass * gravity

TEST(OrganismForceGatheringTest, EmptyCellsContributeZeroForce)
// Only non-empty cells contribute
```

**6. Growth Tests** (`OrganismGrowth_test.cpp`):

```cpp
TEST(OrganismGrowthTest, GrowCellAddsToLocalShape)
// local_shape.size() increases by 1

TEST(OrganismGrowthTest, GrowCellUpdatesMass)
// organism.mass increases by new cell's mass

TEST(OrganismGrowthTest, GrowCellUpdatesCOM)
// organism.center_of_mass shifts toward new cell

TEST(OrganismGrowthTest, AnchorPositionUnchanged)
// organism.position stays fixed during growth
```

### Test-First Implementation Order

We can write tests before implementation for clean TDD:

1. **First**: Write `OrganismPhysics_test.cpp` tests → Implement Organism struct with position/velocity
2. **Second**: Write `OrganismGridProjection_test.cpp` tests → Implement projectToGrid()
3. **Third**: Write `OrganismForceGathering_test.cpp` tests → Implement gatherEnvironmentForces()
4. **Fourth**: Write `OrganismCollision_test.cpp` tests → Implement detectCollisions()
5. **Fifth**: Write `OrganismCollisionResponse_test.cpp` tests → Implement handleCollision_Impulse()
6. **Sixth**: Write `OrganismGrowth_test.cpp` tests → Integrate with existing growth code

This order builds up the system incrementally - each step depends on previous steps working.

## Implementation Status

### Development Strategy: Goose as Testbed

We're using **Goose** as the testbed organism for developing rigid body physics:

1. **Goose** is a new organism type that uses the rigid body approach from the start.
2. It will be extended from single-cell to multi-cell to validate the tearing fix.
3. Once proven, the same approach will be applied to Tree.
4. **Duck** stays as-is (single-cell, cell-based physics works fine).

**Why Goose?**
- Clean slate - no legacy code to work around.
- Can test multi-cell physics without breaking existing Tree functionality.
- Permanent organism type that coexists with Duck.

### Phase 1 - Completed

**Organism Base Class:**
- ✅ Add continuous position/velocity to Organism struct
- ✅ Add LocalCell struct for local shape definition
- ✅ Implement mass computation (recomputeMass)
- ✅ Implement center of mass computation (recomputeCenterOfMass)
- ✅ Implement position integration (integratePosition)
- ✅ Implement force application (applyForce)
- ✅ Write OrganismPhysics_test.cpp (13 tests, all passing)

**Goose Implementation:**
- ✅ Create Goose organism class (Goose.h, Goose.cpp)
- ✅ Create GooseBrain interface and RandomGooseBrain
- ✅ Implement projectToGrid() in Goose
- ✅ Implement gatherForces() in Goose
- ✅ Add GOOSE to OrganismType and EntityType enums
- ✅ Add Goose to OrganismManager (createGoose, getGoose)
- ✅ Add Goose sprite rendering to EntityRenderer
- ✅ Create GooseTestScenario for testing

### Phase 1 - Completed (Collision Detection)

**Collision System:**
- ✅ Add CollisionInfo struct to Organism.h
- ✅ Implement detectCollisions() in Organism base class
  - Checks: WALL, other organisms, dense solids (DIRT/SAND/WOOD/METAL/ROOT > 0.8), world boundaries
  - Returns: blocked status, blocked cells, contact normal
- ✅ Write OrganismCollision_test.cpp (11 tests, all passing)
- ✅ Integrate detectCollisions() into Goose update loop
  - Predict position before moving
  - Check collisions at predicted cells
  - Apply velocity-based collision response

**Testing:**
- ✅ Write Goose_test.cpp (8 tests)
  - TestGooseBrain for controlled testing
  - Stand still, fall to floor, walk left/right
  - Vertical wall collision, organism-organism collision
- ✅ Write OrganismCollision_test.cpp (11 tests, all passing)
  - Empty space, WALL, floor, out of bounds (all 4 edges)
  - Other organism, own cells, dense solids (DIRT), multi-cell collision

**Goose Tests Status:** 4/8 passing
- ✅ CreateGoosePlacesWoodCell
- ✅ GooseStandsStillWithWaitAction
- ✅ GooseFallsToFloorThenStops
- ✅ GooseCannotWalkThroughOtherOrganism
- ❌ GooseWalksRightWhenOnGround (walking works but too fast)
- ❌ GooseWalksLeftWhenOnGround (walking works but too fast)
- ❌ GooseStopsWhenWalkDirectionChangesToZero (velocity issue)
- ❌ GooseCannotWalkThroughVerticalWall (contact normal issue)

### Phase 1 - In Progress

**Collision Response Tuning:**
- [ ] Fix contact normal computation for proper slide behavior
- [ ] Add friction/damping to prevent excessive speed
- [ ] Implement proper impulse-based bounce (handleCollision_Impulse)

**Multi-Cell Goose:**
- [ ] Extend Goose to 1x2 (two cells tall)
- [ ] Verify both cells move together without tearing
- [ ] Extend to 2x2 to test larger shapes

### Phase 1 - Future

- [ ] Migrate Tree to use rigid body physics
- [ ] Remove old velocity sync code from WorldRigidBodyCalculator
- [ ] Consider migrating Duck (optional - single-cell works fine as-is)

### Files Created/Modified

**Organism Base Class:**
| File | Description |
|------|-------------|
| `src/core/organisms/Organism.h` | Added CollisionInfo struct, detectCollisions() method |
| `src/core/organisms/Organism.cpp` | Implemented detectCollisions() with full collision checks |

**Goose Implementation:**
| File | Description |
|------|-------------|
| `src/core/organisms/Goose.h` | Goose class with rigid body physics |
| `src/core/organisms/Goose.cpp` | Collision-aware update loop, projectToGrid(), gatherForces() |
| `src/core/organisms/GooseBrain.h` | Brain interface and RandomGooseBrain |
| `src/core/organisms/GooseBrain.cpp` | Random brain implementation |

**Tests:**
| File | Description |
|------|-------------|
| `src/core/organisms/tests/Goose_test.cpp` | 8 integration tests for Goose physics (4/8 passing) |
| `src/core/organisms/tests/OrganismCollision_test.cpp` | 11 unit tests for detectCollisions() (11/11 passing) |

**Scenario:**
| File | Description |
|------|-------------|
| `src/server/scenarios/scenarios/GooseTestScenario.h` | Test scenario header |
| `src/server/scenarios/scenarios/GooseTestScenario.cpp` | Scenario that creates Goose and Entity |

### Commits

- `e949caf` - Updated design doc for rigid body approach
- `69ce237` - Added comprehensive test plan
- `2d10812` - Implemented organism physics foundation with passing tests
- (next) - Implement collision detection for organisms

---

## Historical Reference: Velocity Sync Approach (OBSOLETE)

The following documents the original approach for reference. **Do not implement this** - it has fundamental flaws described above.

### Original Algorithm: Flood-Fill + Prune + Velocity Sync

1. **FLOOD FILL FROM SEED**
   - Start from tree.seed_position
   - Follow ROOT, WOOD, SEED cells (4-connected)
   - Build set of connected positions

2. **PRUNE DISCONNECTED FRAGMENTS**
   - Any ROOT/WOOD not in connected set loses organism_id
   - Becomes independent particle

3. **APPLY UNIFIED VELOCITY**
   - Gather pending_force from all connected cells
   - acceleration = total_force / total_mass
   - Set all connected cells to same velocity

### Why It Failed

- Unified velocity doesn't unify COM positions
- Each cell's COM still moves independently
- Boundary crossings happen at different times
- Structure tears apart during movement
- No mechanism for collision detection/response
- No reaction forces applied to environment
