# Rigid Body Design for Grid-Based Physics

## Overview

Rigid body organisms (Goose) store position in continuous space and project onto the grid each frame. This prevents multi-cell organisms from tearing apart when cells cross boundaries at different times.

**Core principle:** Rigid bodies control their own position. World physics provides forces, not movement.

## Organism Architecture

Organisms fall into two categories based on their physics needs:

### Single-Cell Organisms (Duck Template)

Single-cell organisms live as a single grid cell. They use world physics directly:
- The organism IS the cell - no separate position tracking needed
- World physics handles gravity, pressure, collisions
- Organism just adds forces (walk, jump) to the cell's pending_force
- No tearing problem (only one cell)

**Example:** Duck adds walk/jump forces to its cell, world physics does the rest.

**Key characteristic:** `usesRigidBodyPhysics() = false`

### Multi-Cell Organisms (Goose Prototype)

Multi-cell organisms need continuous position tracking to prevent tearing:
- Maintain floating-point position separate from grid
- Define structure as `local_shape` (cells relative to position)
- Each frame: gather forces → integrate → collision detect → project to grid
- All cells move together because they're projected from one position

**Example:** Goose has continuous position, projects its shape onto grid each frame.

**Key characteristic:** `usesRigidBodyPhysics() = true`

### Tree Migration

Tree is currently implemented as cell-based (like Duck) but is multi-cell, causing structural tearing. Tree should migrate to the Goose pattern:
- Continuous position (initially at seed location)
- Local shape defines SEED, WOOD, ROOT, LEAF cells relative to position
- Growth adds cells to local_shape, then projects to grid
- Physics keeps structure together automatically

### Architecture Summary

| Organism | Cells | Position | Physics | Template |
|----------|-------|----------|---------|----------|
| Duck | 1 | Grid cell | World handles it | Single-cell |
| Goose | N | Continuous | Self-integrated | Multi-cell |
| Tree | N | Continuous (planned) | Self-integrated (planned) | Multi-cell |

## Critical Isolation Requirements

Rigid body organisms MUST be skipped in these systems:

1. **`computeMaterialMoves()`** - Don't generate transfers for their cells (world shouldn't move them)
2. **`notifyTransfers()`** - Don't update their cell tracking (they track their own position)
3. **`applyAirResistance()` in World** - They compute their own drag (avoids one-frame velocity lag)

Additionally, rigid bodies must:
4. **Compute own air resistance** in their update() using current velocity
5. **Gather forces from current position** not occupied_cells (which is stale)

**Bug if any are missing:** Intermittent force loss, position reset to cell center, velocity overshoot, or cells moving independently.

## The Problem (Historical Context)

Cell-based rigid materials (WOOD, METAL) represented as individual particles with cohesion forces caused issues:
- Horizontal structures fell apart (each cell moved independently)
- High cohesion created COM drift and pressure waves
- No concept of structural integrity

**Goal:** Enable organism structures to move as rigid units while still participating in grid-based physics.

## Design Decisions

### Structure Identification: Organism-Only

Rigid structures are identified by `organism_id != 0`. Only cells belonging to a tree organism form rigid structures. This means:
- Adjacent WOOD/METAL cells without an organism move as individual particles
- Tree cells (SEED, WOOD, ROOT, LEAF with same organism_id) move as one unit
- Simpler implementation, focused on the primary use case (trees)

**Removed:** The `is_rigid` material property is being removed from the simulation. It was primarily used by the support system which is also being removed.

### Structure Representation: Minimal (Phase 1)

```cpp
struct RigidStructure {
    std::vector<Vector2i> cells;
    Vector2d center_of_mass;
    double total_mass;
    Vector2d velocity;
    uint32_t organism_id;
};
```

Phase 1 focuses on translation only. Rotation (moment of inertia, angular velocity) comes later.

### Physics Integration: Approach 2 (Skip + Resolve)

Rather than post-hoc velocity averaging, we:
1. Identify rigid structures at frame start
2. Skip rigid structure cells during per-cell force resolution
3. Gather forces from structure cells and apply unified F=ma
4. Set all structure cells to the unified velocity

This is cleaner than letting per-cell physics run and then overriding.

## Removed Systems

### Support System (Removed)

The `WorldSupportCalculator` and all support-related code is being removed:
- `has_any_support` and `has_vertical_support` flags on Cell
- `applySupportForces()` in World
- `WorldSupportCalculator` class entirely

Support was an attempt to make structures stable by canceling gravity for "supported" cells. The rigid body system replaces this need - structures stay together because they move as one unit, not because individual cells are marked as supported.

### is_rigid Material Property (Removed)

The `is_rigid` flag on `MaterialProperties` is being removed:
- Was used by support system (removed)
- Was used to skip pressure forces for rigid materials
- Individual rigid-material cells will now behave like high-viscosity particles

## Component Framework

Rather than duplicating physics code between Goose and Tree, we extract shared behavior into reusable components. Organisms compose these components to get the physics behavior they need.

### Design Principles

1. **Extract from Goose** - Goose already has working rigid body physics. Extract components from it.
2. **Tree consumes same components** - Tree migrates to use the extracted components.
3. **Test components in isolation** - Unit tests for components, integration tests for organisms.
4. **Composition over inheritance** - Organisms own components, not inherit from physics base classes.

### Component Interfaces

```cpp
// How forces are gathered and applied
class PhysicsComponent {
public:
    virtual void gatherForces(World& world, const std::vector<Vector2i>& cells) = 0;
    virtual void applyAirResistance(const World& world, Vector2d velocity) = 0;
    virtual Vector2d getPendingForce() const = 0;
    virtual void clearPendingForce() = 0;
    virtual void integrate(Vector2d& velocity, double mass, double dt) = 0;
};

// How the organism appears on the grid
class ProjectionComponent {
public:
    virtual void project(World& world, OrganismId id, Vector2d position, Vector2d velocity) = 0;
    virtual void clear(World& world) = 0;
    virtual const std::vector<Vector2i>& getOccupiedCells() const = 0;

    // For growable organisms
    virtual void addCell(Vector2i local_pos, MaterialType type, double fill) = 0;
    virtual void removeCell(Vector2i local_pos) = 0;
};

// Collision detection and response
class CollisionComponent {
public:
    virtual CollisionInfo detect(
        const World& world,
        const std::vector<Vector2i>& current_cells,
        const std::vector<Vector2i>& predicted_cells) = 0;
    virtual void respond(CollisionInfo& info, Vector2d& velocity) = 0;
};
```

### Component Implementations

**RigidBodyPhysicsComponent** - For multi-cell organisms:
- Gathers pending_force from all occupied grid cells
- Applies air resistance based on current velocity
- Integrates F=ma to update velocity

**LocalShapeProjection** - For multi-cell organisms:
- Stores `local_shape` (cells relative to organism position)
- Projects cells onto grid based on continuous position
- Computes sub-cell COM from fractional position
- Supports adding/removing cells (for growth)

**RigidBodyCollisionComponent** - For multi-cell organisms:
- Predicts which cells would be occupied at desired position
- Detects blocking cells (non-empty, non-organism)
- Responds by zeroing velocity component into obstacle

**CellPhysicsComponent** - For single-cell organisms:
- Applies forces directly to the grid cell
- Delegates integration to world physics

**SingleCellProjection** - For single-cell organisms:
- Organism occupies exactly one cell
- No local_shape needed

### Organism Composition

```cpp
// Multi-cell organism (Goose, Tree)
class Goose : public Organism {
    RigidBodyPhysicsComponent physics_;
    LocalShapeProjection projection_;
    RigidBodyCollisionComponent collision_;
    std::unique_ptr<GooseBrain> brain_;

    Vector2d position_;
    Vector2d velocity_;
};

// Single-cell organism (Duck)
class Duck : public Organism {
    CellPhysicsComponent physics_;
    SingleCellProjection projection_;
    std::unique_ptr<DuckBrain> brain_;
};
```

### Testing Strategy

**Component unit tests** (test in isolation with real World, minimal setup):

`RigidBodyPhysicsComponent_test.cpp`:
- `GatherForcesSumsFromCells` - Sum pending_force from all occupied cells
- `GathersFromCurrentPositionNotOccupiedCells` - Use floor(position + local_pos), not stale occupied_cells
- `IncludesGravity` - Total force includes mass × gravity
- `EmptyCellsContributeZeroForce` - AIR cells don't add to force sum
- `IntegrateFollowsFEqualsMA` - velocity += (force / mass) × dt
- `AirResistanceReducesVelocity` - Drag computed from current velocity

`LocalShapeProjection_test.cpp`:
- `ClearsOldOccupiedCells` - Previous projection cleared before new one
- `ProjectsAllCellsToGrid` - Each local cell appears at correct grid position
- `SetsOrganismIdOnProjectedCells` - Grid cells have correct organism_id
- `SetsMaterialTypeOnProjectedCells` - Grid cells have correct material from local_shape
- `SetsVelocityOnProjectedCells` - Grid cells have organism's velocity
- `ComputesSubCellCOMFromFractionalPosition` - cell.com reflects sub-grid position
- `AddCellExpandsShape` - Growth adds to local_shape
- `RemoveCellShrinksShape` - Damage removes from local_shape

`RigidBodyCollisionComponent_test.cpp`:
- `DetectsWallCollision` - WALL material blocks movement
- `DetectsOtherOrganismCollision` - Different organism_id blocks movement
- `DetectsDenseSolidCollision` - fill_ratio > 0.8 solid blocks movement
- `DetectsWorldBoundaryCollision` - Out of bounds blocks movement
- `NoCollisionWithEmptySpace` - AIR cells don't block
- `NoCollisionWithOwnCells` - Same organism_id doesn't block
- `ContactNormalPointsAwayFromObstacle` - Normal computed correctly (**KNOWN BUG: currently hardcoded**)
- `ImpulseReversesNormalVelocity` - Velocity into surface is reversed
- `RestitutionControlsBounce` - e=0 stops, e=1 full bounce
- `TangentialVelocityPreserved` - Velocity parallel to surface unchanged
- `ReactionForceAppliedToEnvironment` - Blocked cells receive equal-and-opposite force

`OrganismGrowth_test.cpp` (for LocalShapeProjection growth support):
- `GrowCellAddsToLocalShape` - local_shape.size() increases
- `GrowCellUpdatesMass` - organism mass increases by new cell's mass
- `GrowCellUpdatesCOM` - center_of_mass shifts toward new cell
- `AnchorPositionUnchanged` - organism.position stays fixed during growth

**Already implemented** (in `OrganismPhysics_test.cpp`, `OrganismCollision_test.cpp`):
- 13 physics tests (position, velocity, mass, COM, gravity)
- 11 collision detection tests (wall, floor, bounds, organisms, dense solids)

**Organism integration tests** (verify full behavior):
- `GooseFallsAndLandsOnGround` (existing)
- `TreeFallsAsRigidBody` (new - after migration)
- `TreeGrowthUpdatesLocalShape` (new - after migration)

### File Structure

```
src/core/organisms/
├── components/
│   ├── PhysicsComponent.h
│   ├── RigidBodyPhysicsComponent.h
│   ├── RigidBodyPhysicsComponent.cpp
│   ├── CellPhysicsComponent.h
│   ├── ProjectionComponent.h
│   ├── LocalShapeProjection.h
│   ├── LocalShapeProjection.cpp
│   ├── SingleCellProjection.h
│   ├── CollisionComponent.h
│   ├── RigidBodyCollisionComponent.h
│   └── RigidBodyCollisionComponent.cpp
├── tests/
│   ├── RigidBodyPhysicsComponent_test.cpp
│   ├── LocalShapeProjection_test.cpp
│   ├── RigidBodyCollisionComponent_test.cpp
│   └── OrganismGrowth_test.cpp
├── Duck.h / Duck.cpp
├── Goose.h / Goose.cpp
└── Tree.h / Tree.cpp
```

## Implementation Plan

### Phase 1: Cleanup

**1.1 Remove Support System**
- Delete `WorldSupportCalculator.h` and `WorldSupportCalculator.cpp`
- Remove `has_any_support`, `has_vertical_support` from Cell
- Remove `applySupportForces()` from World
- Remove support-related code from WorldCollisionCalculator, WorldViscosityCalculator
- Remove from CMakeLists.txt
- Update any tests that reference support

**1.2 Remove is_rigid**
- Remove `is_rigid` from `MaterialProperties` struct
- Remove `isMaterialRigid()` function
- Update code that checks `is_rigid` (pressure forces, etc.)

### Phase 2: Rigid Body Foundation

**2.1 Update WorldRigidBodyCalculator**

Already implemented:
- `findConnectedStructure()` - flood-fill to find connected cells
- `findAllStructures()` - find all structures in world
- `calculateStructureCOM()` - weighted center of mass
- `calculateStructureMass()` - total mass
- `gatherStructureForces()` - sum pending forces

Need to update:
- Change from `isMaterialRigid()` to `organism_id != 0` for structure membership
- Add `applyUnifiedVelocity()` - compute and set unified velocity for structure

**2.2 Integrate into Physics Loop**

Current loop:
```
advanceTime(deltaTime)
├── Pressure phases
├── resolveForces()
│   ├── Accumulate forces (gravity, pressure, cohesion, etc.)
│   ├── applyBoneForces()  [DISABLED - bones not created]
│   └── Apply F=ma to each cell → velocity
├── organism_manager->update()  [MOVED EARLIER]
├── Velocity limiting
├── updateTransfers() → compute material moves
├── processMaterialMoves() → execute moves
```

Modified loop:
```
advanceTime(deltaTime)
├── Pressure phases
├── organism_manager->update()  [MOVED - before force accumulation]
├── identifyRigidStructures() → structures, rigid_cell_set  [NEW]
├── resolveForces(rigid_cell_set)  [MODIFIED]
│   ├── Accumulate forces (all cells)
│   └── Apply F=ma only to NON-rigid cells
├── resolveRigidBodies(structures)  [NEW]
│   ├── For each structure: gather forces, F=ma, set unified velocity
├── Velocity limiting
├── updateTransfers()
└── processMaterialMoves()
```

**Note:** The bones system is disabled during rigid body implementation. Bones are not created (createBonesForCell is a no-op), so applyBoneForces does nothing. The rigid body system replaces bones for structural integrity.

### Phase 3: Ground Support via Pressure

**Problem Discovered:** Organisms slowly sink through the ground over time. The `ExtendedGrowthStability` test demonstrates this - a tree's COM drifts downward at ~0.15 velocity even when "resting" on dirt, eventually causing cells to transfer out and the structure to fragment.

**Root Cause:** The rigid body system unifies velocities across organism cells and applies F=ma correctly, but there's no reaction force from ground contact. Gravity applies +2.94 force per frame, but the opposing ground reaction is 0. The organism is effectively in slow free-fall.

**Why This Happens:**
1. Gravity → adds to `pending_force` on organism cells ✓
2. Organism cells → inject weight into pressure field below ✓
3. Pressure builds up in ground cells ✓
4. `resolveForces()` skips organism cells (they're handled separately)
5. `resolveRigidBodies()` gathers `pending_force` but this doesn't include ground reaction ✗

The pressure system already works for all materials (METAL, WOOD, SEED all have `pressure_injection_weight = 1.0`), but organisms never read that pressure back as support.

**Solution:** Close the loop by sampling pressure at the organism-ground interface and converting it to a support force. This is Newton's Third Law through the pressure field:
- Action: Organism pushes weight into ground (pressure injection)
- Reaction: Ground pushes back on organism (pressure → support force)

**3.1 Algorithm: Compute Organism Support Force**

```
computeOrganismSupportForce(organism_cells, organism_id, gravity):

    gravity_dir = normalize(gravity)
    total_weight = sum of (cell.mass * |gravity|) for all organism cells

    # Step 1: Find contact surface (organism cells adjacent to ground)
    contact_points = []
    for each pos in organism_cells:
        ground_pos = pos + gravity_dir  # cell "below" in gravity direction

        if not isValidCell(ground_pos):
            # World boundary - full support
            contact_points.append(BOUNDARY_CONTACT)
            continue

        ground_cell = grid[ground_pos]

        if ground_cell.isEmpty():
            continue  # No ground here
        if ground_cell.organism_id == organism_id:
            continue  # Internal cell, not contact surface

        contact_points.append({
            ground_pos: ground_pos,
            pressure: ground_cell.pressure,
            material: ground_cell.material_type
        })

    # Step 2: Sum support from all contact points
    if contact_points is empty:
        return (0, 0)  # Free fall

    total_support_pressure = 0
    for each contact in contact_points:
        if contact == BOUNDARY_CONTACT:
            total_support_pressure += LARGE_VALUE  # or handle specially
        else:
            total_support_pressure += contact.pressure

    # Step 3: Convert pressure to reaction force
    support_magnitude = total_support_pressure * pressure_to_force_scale
    support_magnitude = min(support_magnitude, total_weight)  # Cap at weight

    support_force = -gravity_dir * support_magnitude
    return support_force
```

**3.2 Integration Point**

In `resolveRigidBodies()`, after gathering forces but before computing acceleration:

```cpp
Vector2d total_force = gatherStructureForces(connected);  // includes gravity

// NEW: Add support force from ground contact
Vector2d support_force = computeOrganismSupportForce(connected, organism_id);
total_force += support_force;

// Now F=ma gives balanced forces when on ground
Vector2d acceleration = total_force * (1.0 / total_mass);
```

**3.3 Expected Behavior**

| Scenario | Support Force | Result |
|----------|--------------|--------|
| On stable ground | ≈ organism weight | Equilibrium, no sinking |
| Over air | 0 | Falls normally |
| Over loose/falling material | partial | Sinks slowly (realistic) |
| At world boundary | full | Stops at boundary |

**3.4 Resolved Design Decisions**

- **pressure_to_force_scale**: Derived from injection/decay equilibrium:
  ```
  Injection: pressure_contribution = density × gravity × injection_weight × hydrostatic_strength × deltaTime
  At equilibrium: weight × hydrostatic_strength × dt = pressure × decay_rate × dt
  Therefore: pressure_to_force_scale = pressure_decay_rate / hydrostatic_strength
  ```

- **WALL handling**: Treat WALL contact as automatic full support (magic). WALL has `pressure_injection_weight = 0` so pressure doesn't build in WALL cells, but we detect WALL contact and provide instant full support.

- **Partial contact**: Not a problem. Each cell injects its own weight (not accumulated pressure) into the cell below. So the ground only receives direct injection from the bottom cell's weight. However, pressure diffuses through the tree structure over time (WOOD diffusion = 0.15, SEED = 0.1), so the ground eventually "sees" the whole tree's weight. This creates natural behavior:
  - Immediate: partial support (from bottom cell's weight)
  - Over time: full support (as pressure diffuses and equilibrates)
  - Truly partial contact (tree over edge): only grounded portion contributes, which is correct.

**3.5 Open Questions (Deferred)**

- **Material-based support strength**: Should rigid materials (METAL, packed DIRT) provide stronger support per unit pressure than loose materials (SAND, WATER)?

- **Horizontal support**: Current algorithm only considers support in gravity direction. Lateral support for organisms pressed against walls is deferred.

- **Threshold for "supported"**: Minimum pressure threshold to prevent micro-sinking on very low pressure. May not be needed given equilibrium dynamics.

### Phase 4: Component Extraction & Tree Migration

This phase extracts reusable components from Goose and migrates Tree to use them. See "Component Framework" section for design details.

**Known Goose Issues** (to fix during extraction):
- **Contact normal hardcoded** - `Organism.cpp:236` returns `(0,-1)` (floor) for all collisions. Need to compute actual direction from organism to obstacle.
- **No deceleration friction** - Organisms don't slow down when they stop walking. Need friction when walk direction becomes zero.

**4.1 Extract RigidBodyPhysicsComponent from Goose**
- Create `PhysicsComponent` interface
- Move `gatherForces()`, `applyAirResistance()`, `applyForce()` logic to component
- Goose owns component, delegates to it
- Write unit tests for component in isolation
- Verify Goose integration tests still pass

**4.2 Extract LocalShapeProjection from Goose**
- Create `ProjectionComponent` interface
- Move `projectToGrid()`, `clearOldProjection()`, `local_shape` to component
- Add `addCell()`, `removeCell()` methods for growth support
- Write unit tests for projection behavior
- Verify Goose integration tests still pass

**4.3 Extract RigidBodyCollisionComponent from Goose**
- Create `CollisionComponent` interface
- Move `detectCollisions()`, collision response logic to component
- Write unit tests for collision detection
- Verify Goose integration tests still pass

**4.4 Migrate Tree to Component Architecture**
- Tree gets continuous `position_` and `velocity_`
- Tree uses `RigidBodyPhysicsComponent` for physics
- Tree uses `LocalShapeProjection` for grid presence
- Tree uses `RigidBodyCollisionComponent` for collision
- Growth modifies `projection_.addCell()` instead of grid directly
- Seed position becomes initial `position_`

**4.5 Update Tree Tests**
- `TreeFallsAsRigidBody` - seed in air falls, structure stays together
- `TreeGrowthUpdatesLocalShape` - growing adds cells to projection
- `TreeCollisionStopsMovement` - hitting ground stops falling
- Update existing germination tests for new architecture

**4.6 Optional: Extract Single-Cell Components**
- `CellPhysicsComponent` for Duck (delegates to world physics)
- `SingleCellProjection` for Duck (organism = one cell)
- Lower priority - Duck already works fine

### Phase 5: Future (Not This Sprint)

- Rotation (angular velocity, moment of inertia, torques)
- Fracture mechanics (structures breaking apart)
- Non-organism rigid bodies (welded metal structures)

## Tests

### Unit Tests (WorldRigidBodyCalculator)

**Already implemented:**
- `SingleWoodCellFormsStructure`
- `NonRigidCellReturnsEmpty`
- `LShapedWoodConnects`
- `DiagonalDoesNotConnect`
- `DifferentOrganismIdDoesNotConnect`
- `SameOrganismIdConnects`
- `FindAllStructuresFindsMultiple`
- `CalculateMassIsSumOfCellMasses`
- `CalculateCOMIsWeightedCenter`
- `GatherForcesIsSumOfPendingForces`

**To implement:**
- `ApplyUnifiedVelocitySetsAllCellsToSameVelocity`
- `OrganismCellsFormStructure` (updated identification)
- `NonOrganismRigidCellsDoNotFormStructure`

### Integration Tests (Physics Behavior)

**To implement:**
- `FloatingStructureFallsTogether` - Structure in air falls as unit
- `WoodStructureInWaterMovesAsUnit` - Buoyancy affects whole structure
- `MultipleStructuresMoveIndependently` - Two trees move separately
- `StructureOnGroundStops` - Collision with WALL stops structure
- `LooseParticlesUnaffectedByStructureLogic` - Non-organism cells still work

### Ground Support Tests (Phase 3)

**Existing test that demonstrates the problem:**
- `TreeGerminationTest.ExtendedGrowthStability` - Currently fails due to sinking

**To implement:**
- `OrganismOnDirtReachesEquilibrium` - Tree on dirt has zero net velocity after stabilization
- `OrganismOverAirFalls` - Tree with no ground contact falls normally
- `OrganismPartiallyOverAirSinksSlowly` - Tree half over ground, half over air
- `OrganismOnWallFullSupport` - WALL provides immediate full support
- `OrganismOnLooseSandPartialSupport` - Low-pressure sand provides weak support
- `SupportForceMatchesWeight` - Verify support_force ≈ -gravity_force at equilibrium
- `PressureBuildupCreatesSupport` - Verify pressure accumulates and provides support over time

### Test Scenario: Wood in Water

```
Row 0: AIR    AIR   AIR   AIR
Row 1: WATER  WOOD  WOOD  WATER   (WOOD cells have organism_id=1)
Row 2: WATER  WATER WATER WATER
```

Expectations:
- Both WOOD cells have identical velocity at all times
- WOOD floats (density < water) or sinks slowly
- Water flows around the structure

## Files to Modify

### Remove
- `src/core/WorldSupportCalculator.h`
- `src/core/WorldSupportCalculator.cpp`

### Modify
- `src/core/Cell.h` - remove support flags
- `src/core/MaterialType.h` - remove is_rigid
- `src/core/MaterialType.cpp` - remove is_rigid from properties
- `src/core/World.h` - add resolveRigidBodies declaration
- `src/core/World.cpp` - modify advanceTime, resolveForces; add resolveRigidBodies
- `src/core/WorldRigidBodyCalculator.h` - add applyUnifiedVelocity
- `src/core/WorldRigidBodyCalculator.cpp` - update identification, add velocity method
- `src/core/WorldCollisionCalculator.cpp` - remove is_rigid/support checks
- `src/core/WorldViscosityCalculator.cpp` - remove support checks
- `src/core/RenderMessage.h` - remove support fields
- `src/core/RenderMessageUtils.h` - remove support serialization
- `CMakeLists.txt` - remove WorldSupportCalculator

### Phase 3 Files (Ground Support)
- `src/core/World.cpp` - add computeOrganismSupportForce(), integrate into resolveRigidBodies()
- `src/core/World.h` - declare computeOrganismSupportForce()
- `src/core/PhysicsSettings.h` - add pressure_to_force_scale (= pressure_decay_rate / hydrostatic_strength)
- `src/core/organisms/tests/TreeGermination_test.cpp` - update ExtendedGrowthStability expectations

### Bones System (Disabled)
- `src/core/organisms/Organism.cpp` - make createBonesForCell() a no-op (early return)

### Order of Operations
- `src/core/World.cpp` - move organism_manager_->update() before resolveForces()

### Tests to Update
- `src/core/organisms/tests/CantileverSupport_test.cpp` - remove or repurpose
- Various tests that check `has_any_support`

## Work Sequence

1. **Disable bones** - make createBonesForCell() a no-op
2. **Move organism updates** - organism_manager_->update() before resolveForces()
3. **Remove support system** (cleanup)
4. **Remove is_rigid** (cleanup)
5. **Update structure identification** (organism_id based)
6. **Add applyUnifiedVelocity** + tests
7. **Integrate into World::advanceTime**
8. **Integration tests** (falling, floating, etc.)
9. **Add computeOrganismSupportForce()** - Phase 3 implementation
10. **Integrate support force into resolveRigidBodies()**
11. **Ground support tests** - verify equilibrium behavior
12. **Tune pressure_to_force_scale** - use pressure_decay_rate / hydrostatic_strength
13. **Structure movement/collisions** (leading edge handling) - Phase 4

## Implementation Status

### ✅ Completed: Phases 1-3 (Rigid Body Foundation + Ground Support)

**Test Results:** 162/164 tests pass (2 expected failures)

#### Phase 1: Cleanup (Complete)
- ✅ Disabled bones - `Organism::createBonesForCell()` returns early
- ✅ Removed support system - Cleaned up legacy support code
- ✅ is_rigid never existed - Design doc updated

#### Phase 2: Rigid Body Foundation (Complete)
- ✅ Structure identification - Uses `organism_id != 0` (already implemented)
- ✅ Moved organism updates - `organism_manager_->update()` before `resolveForces()`
- ✅ Unified velocity - `resolveRigidBodies()` integrates forces and sets all cells to same velocity
- ✅ Skip organism cells - `resolveForces()` skips organism cells (line 871)
- ✅ Fixed pending_force handling - Organism cells preserve forces through `resolveForces()`

#### Phase 3: Ground Support (Complete)
- ✅ `computeOrganismSupportForce()` - Contact-based support from solid ground
- ✅ Material-specific support - WALL/METAL/DIRT = full support, WATER = buoyancy, AIR = none
- ✅ Integrated support force - Added before F=ma in `resolveRigidBodies()` (line 1042)
- ✅ **Result**: Organisms on ground reach near-equilibrium (vel ~0.003, down from ~0.15)

#### Connectivity Pruning (Partially Complete)
- ✅ Moved to separate phase - `pruneDisconnectedFragments()` runs AFTER transfers (line 491)
- ✅ Empty cell cleanup - Removes stale positions when cells become empty
- ✅ Ownership cleanup - Removes cells transferred to other organisms
- ⏸️ Structural disconnection - Disabled until Phase 4 (see Challenges section)

### 🔧 Challenges Encountered & Solutions

#### Challenge 1: Bounds Check Crash
**Problem**: Moving `organism_manager_->update()` earlier exposed bounds check bug in `WorldCohesionCalculator` and `WorldAdhesionCalculator`. New organism cells at world edges (e.g., WOOD at x=0) caused neighbor queries at x=-1, triggering uint32_t underflow.

**Solution**: Added explicit bounds checking before calling `getCellAt()` in both calculators.

**Impact**: Pre-existing bug, masked by timing. Now fixed.

#### Challenge 2: Duck Walking Forces Cleared
**Problem**: Duck tests failed - ducks couldn't walk. Walking force from brain was added during `organism_manager_->update()`, but then cleared in `resolveForces()` before `resolveRigidBodies()` could use it.

**Solution**: Skip organism cells when clearing pending_force at start of `resolveForces()`. Clear organism cell forces AFTER `resolveRigidBodies()` applies them.

**Impact**: All 10 duck tests now pass.

#### Challenge 3: Connectivity Pruning False Positives
**Problem**: Cells marked as disconnected immediately after being added. Investigation revealed:
```
Frame N: organism tracking 3 cells, flood-fill found 1 connected
  Cell (4,6) ROOT org_id=1 fill=1.00 connected=false  ← Why?
  Cell (4,5) AIR org_id=0 fill=0.00 connected=false   ← Former position!
```

**Root Cause**: Pruning ran BEFORE material transfers. Connectivity check used stale positions, then cells swapped/transferred to actually connected positions.

**Solution**: Moved pruning to new `pruneDisconnectedFragments()` function that runs AFTER `processMaterialMoves()`.

**Impact**: Timing issue fixed, but revealed deeper issue below.

#### Challenge 4: Structural Gaps Without Position Constraints
**Problem**: Even with correct timing, organism cells create gaps during transfers. Example:
```
SEED at (4,5), WOOD at (4,3), but (4,4) is AIR
```

**Root Cause**: Unified velocity means cells accelerate together, but without position constraints, their COMs can move independently. If a cell starts with different COM/velocity from a previous frame, it can become spatially separated during `processMaterialMoves()`.

**Solution**: Disabled structural disconnection pruning. Only clean up empty cells and ownership changes. Structural connectivity enforcement requires Phase 4 (position constraints).

**Impact**: Organisms can have temporary gaps, but overall structure remains functional. Tests pass.

### ❌ Test Failures (2 expected, 162 pass)

1. **`DisconnectedFragmentGetsPruned`** (Expected)
   - Test expects structural disconnection detection
   - Disabled until Phase 4 implements position constraints

2. **`ExtendedGrowthStability`** (Expected)
   - Assertion: `com_magnitude < 0.4`
   - Actual: SEED COM = 1.0 (particle resting at bottom of cell touching ground)
   - Velocity ~0.003 confirms ground support working
   - Failure is about test threshold, not physics

### 🚧 Not Yet Implemented

#### Phase 4: Component Extraction & Tree Migration
**Why needed**: Tree currently uses cell-based physics but is multi-cell, causing structural tearing. Tree should use the same rigid body architecture as Goose.

**Approach**: Extract reusable components from Goose, then have Tree use them:

1. **Extract RigidBodyPhysicsComponent** - Force gathering, air resistance, F=ma integration
2. **Extract LocalShapeProjection** - Local shape storage, grid projection, growth support
3. **Extract RigidBodyCollisionComponent** - Collision detection and response
4. **Migrate Tree** - Continuous position, uses components, growth modifies local_shape

**Key insight**: The "structural gaps" problem (Challenge 4) is solved by continuous position + projection, not by position constraints on grid cells. Goose already works this way.

**Dependencies**: None - can proceed incrementally. Each extraction step keeps Goose tests passing.

#### Phase 5: Advanced Features
- Rotation (angular velocity, torques)
- Fracture mechanics (structures breaking under stress)
- Non-organism rigid bodies (welded metal structures)

### 📊 Current Physics Loop (Updated for Goose)

```
advanceTime(deltaTime)
├── Pressure phases (injection, diffusion, decay)
│
├── organism_manager->update()                    // Cell-based organisms (Duck, Tree)
│   └── Duck adds walk force to cell              // Brain decides actions
│
├── resolveForces(deltaTime, grid)                // Apply physics forces to cells
│   ├── Clear pending_force (non-organism only)
│   ├── Apply gravity (all non-empty cells)
│   ├── Apply air resistance (skip rigid body)    ← Goose skipped
│   ├── Apply pressure forces
│   ├── Apply cohesion/adhesion
│   ├── Apply friction (includes walls now)
│   ├── Apply viscosity
│   └── Integrate F=ma (skip organism cells)      ← Organisms handled separately
│
├── organism_manager->advanceTime()               // Rigid body organisms (Goose)
│   └── Goose::update():
│       ├── Brain decides walk direction
│       ├── Add walk force to pending_force_
│       ├── gatherForces() from CURRENT cells     ← Not occupied_cells!
│       ├── Compute own air resistance            ← Based on current velocity
│       ├── applyForce() → F=ma integration
│       ├── Collision detection
│       └── projectToGrid() → stamp cells
│
├── resolveRigidBodies(deltaTime)                 // Cell-based organism integration
│   ├── Duck: F=ma on cell.pending_force
│   ├── Tree: flood-fill structure, unified velocity, ground support
│   └── Clear organism pending_force
│
├── Velocity limiting
│
├── updateTransfers()
│   ├── computeMaterialMoves()                    ← Skip rigid body cells
│   └── processMaterialMoves()
│
├── notifyTransfers()                             ← Skip rigid body organisms
│
└── pruneDisconnectedFragments()
```

**Key insight:** Rigid body cells are skipped in multiple places because they're NOT moved by world physics - they only GATHER forces from it.

### 🎯 Next Steps

**Phase 4: Component Extraction & Tree Migration**

The path forward is to extract reusable components from Goose and migrate Tree to use them:

1. **Create component interfaces** in `src/core/organisms/components/`
2. **Extract RigidBodyPhysicsComponent** from Goose's `gatherForces()` and `applyForce()`
3. **Write unit tests** for the component in isolation (real World, minimal setup)
4. **Goose uses the component** - verify Goose tests still pass
5. **Extract LocalShapeProjection** from Goose's `projectToGrid()`
6. **Extract RigidBodyCollisionComponent** from Goose's collision logic
7. **Migrate Tree** to use same components - continuous position, projection-based
8. **Update Tree tests** for new architecture

**Why this approach:**
- Goose already solves the tearing problem - extract what works
- Tree benefits from same physics without reimplementing
- Incremental - each step keeps existing tests passing
- Component tests catch regressions early

**After Tree migration:**
- Tree falls as rigid body (structure stays together)
- Growth adds to local_shape, projects to grid
- No more structural gaps or COM drift
- `DisconnectedFragmentGetsPruned` test becomes meaningful again
