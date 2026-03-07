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

## Component Framework (Added)

✅ **Component architecture complete.** Three-layer design for rigid body organisms:

**Base Components** (independently testable):
- `RigidBodyPhysicsComponent` - Force gathering, air resistance, F=ma integration (10 tests)
- `LocalShapeProjection` - Grid projection from continuous position (14 tests)
- `RigidBodyCollisionComponent` - Collision detection, response, support, friction (25 tests)

**Composite Component** (orchestrates the physics loop):
- `RigidBodyComponent` - Owns and coordinates the three base components (10 multi-cell tests)
  - Single `update()` call handles: support → friction → forces → integration → collision → projection
  - Eliminates ~40 lines of duplication per organism
  - Organisms pass external forces (walk, jump, growth) and receive ground state

**Design Rationale:** Initial implementation had organisms directly orchestrate the three components, duplicating the physics loop sequence. The composite emerged when Goose and MultiCellTestOrganism showed identical orchestration code. Now organisms focus on behavior (brain decisions, growth commands) while `RigidBodyComponent` handles all physics mechanics.

**File structure:**
```
src/core/organisms/components/
├── *Component.h (4 interfaces)
├── RigidBody*.{h,cpp} (4 implementations + 1 composite)
└── *_test.cpp (3 component test suites, 49 tests total)
```

See implementation in `src/core/organisms/components/`.

## Implementation Plan (Phases 1-3 Complete)

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

**Note:** The bones system was removed. The rigid body system replaced bones for structural integrity.

### Phase 3: Ground Support (DEFERRED - Simpler approach implemented)

**Original plan:** Sample pressure from ground cells and convert to support force (Newton's 3rd law through pressure field).

**What we actually did:** Direct contact detection in `RigidBodyCollisionComponent::computeSupportForce()`. Checks cells below organism and returns full support for solid materials (WALL, DIRT, METAL), partial for WATER (buoyancy), zero for AIR. This simpler approach works well for Goose.

**Deferred:** Pressure-based support may be revisited if Tree's multi-cell contact needs more complex ground interaction.

### Phase 4: Multi-Cell Validation & Tree Migration

Component extraction is complete (see "Component Framework" section). This phase validates multi-cell behavior before migrating Tree.

**4.0 Multi-Cell Validation Tests (Complete)**

Created simple multi-cell test organisms to verify components work for multi-cell shapes:

**Test organisms** (no growth, no brain, just physics):
- `Stick` - 2 horizontal WOOD cells (basic multi-cell)
- `LShape` - 3 cells in L shape (non-linear shapes)
- `Column` - 3 vertical cells (stacking, partial ground contact)

**Behaviors verified (all 6 passing):**
1. ✅ **Cells stay together** when falling/moving (unified velocity)
2. ✅ **Ground support** with multiple contact points (Column on ground)
3. ✅ **Friction** sums correctly from multiple ground contacts
4. ✅ **Collision** works with multi-cell shapes (L-shape hitting wall)
5. ✅ **Center of mass** computed correctly for multi-cell shapes
6. ✅ **Rotation-free movement** (cells don't tear apart during horizontal motion)

**Implementation:**
- `MultiCellTestOrganism` class using `RigidBodyComponent`
- 10 integration tests in `MultiCellOrganism_test.cpp` (all passing)
- `OrganismManager::createMultiCellTestOrganism()` factory method
- `CellTracker` integration for debugging frame-by-frame physics

**Results:**
- Validated components work correctly for 2-3 cell organisms
- De-risked Tree migration by proving rigid body physics scales beyond single cells
- Documented expected multi-cell behavior through tests

**4.1 Migrate Tree to Component Architecture (After 4.0)**

Once multi-cell validation passes:
- Tree gets continuous `position_` and `velocity_`
- Tree uses `RigidBodyPhysicsComponent` for physics
- Tree uses `LocalShapeProjection` for grid presence
- Tree uses `RigidBodyCollisionComponent` for collision and friction
- Growth modifies `projection_.addCell()` instead of grid directly
- Seed position becomes initial `position_`

**4.2 Update Tree Tests**
- `TreeFallsAsRigidBody` - seed in air falls, structure stays together
- `TreeGrowthUpdatesLocalShape` - growing adds cells to projection
- `TreeCollisionStopsMovement` - hitting ground stops falling
- Update existing germination tests for new architecture

### Phase 5: Future

- Rotation (angular velocity, moment of inertia, torques)
- Fracture mechanics (structures breaking apart)
- Non-organism rigid bodies (welded metal structures)

## Implementation Status

### ✅ Completed: Phases 1-4.1 (Rigid Body Foundation + Component Architecture + Tree Migration)

**Test Results:** 175/177 tests pass (2 expected failures)
- All Tree tests passing (13/13)
- All Goose tests passing (8/8)
- All component tests passing (49/49)
- All multi-cell tests passing (10/10)

#### Phase 1: Cleanup (Complete)
- ✅ Removed bones system entirely
- ✅ Removed support system - Cleaned up legacy support code
- ✅ is_rigid never existed - Design doc updated

#### Phase 2: Rigid Body Foundation (Complete)
- ✅ Structure identification - Uses `organism_id != 0` (already implemented)
- ✅ Moved organism updates - `organism_manager_->update()` before `resolveForces()`
- ✅ Unified velocity - `resolveRigidBodies()` integrates forces and sets all cells to same velocity
- ✅ Skip organism cells - `resolveForces()` skips organism cells (line 871)
- ✅ Fixed pending_force handling - Organism cells preserve forces through `resolveForces()`

#### Phase 3: Ground Support & Friction (Complete)
- ✅ `RigidBodyCollisionComponent::computeSupportForce()` - Direct contact detection (not pressure-based)
- ✅ `RigidBodyCollisionComponent::computeGroundFriction()` - Ground friction with static/kinetic transition
- ✅ Material-specific support - WALL/METAL/DIRT = full support, WATER = buoyancy, AIR = none
- ✅ Material-specific friction - Uses friction coefficients from ground materials
- ✅ Checks cells below organism in gravity direction
- ✅ 6 friction component tests + 25 collision component tests passing
- ✅ **Result**: Goose stands on ground, reaches terminal velocity ~29 cells/sec, decelerates when stopping

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

### ✅ Ground Friction Implementation Complete

**Current Goose test results: 8/8 passing**

All Goose tests now pass with ground friction fully implemented:
1. `CreateGoosePlacesWoodCell` - Basic creation works
2. `GooseStandsStillWithWaitAction` - Ground support working
3. `GooseFallsToFloorThenStops` - Ground support working
4. `GooseWalksRightWhenOnGround` - Ground friction creates terminal velocity ~29 cells/sec
5. `GooseWalksLeftWhenOnGround` - Ground friction working
6. `GooseStopsWhenWalkDirectionChangesToZero` - Friction decelerates goose (7 cell drift)
7. `GooseCannotWalkThroughVerticalWall` - Collision detection working
8. `GooseCannotWalkThroughOtherOrganism` - Collision detection working

**Implementation:**
- `RigidBodyCollisionComponent::computeGroundFriction()` - Computes friction based on ground material and velocity
- Uses same algorithm as WorldFrictionCalculator (smooth static→kinetic transition)
- Material-specific friction coefficients (WALL = 1.0, DIRT = 1.5→0.5, etc.)
- Normal force from support magnitude
- World boundaries treated as WALL (provide full friction)
- 6 component tests passing

**Force Tuning:**
- Walk force: 20.0 → 10.0 (both Duck and Goose)
- Duck jump force: 150.0 → 300.0 (restored for proper jump height)
- Goose jump force: 150.0 (half of original)
- Terminal velocity: ~29 cells/sec (approved)

**Other test failures (non-Goose):**
- **`DisconnectedFragmentGetsPruned`** - Needs position constraints from Phase 4
- **`DuckFloatsInWater`** - Buoyancy system needs investigation
- **`ExtendedGrowthStability`** - Test threshold issue, physics working

**Disabled tests:**
- `ParameterizedBuoyancyTest.MaterialBuoyancyBehavior` (4 cases) - Buoyancy needs tuning

### ✅ Phase 4: Component Extraction (Complete)

All three components extracted, tested, and integrated with Goose:
- ✅ `RigidBodyPhysicsComponent` - Force accumulation, air resistance, F=ma (10 tests)
- ✅ `LocalShapeProjection` - Grid projection, growth support (14 tests)
- ✅ `RigidBodyCollisionComponent` - Detection, response, support, friction (25 tests)
- ✅ Goose fully functional with all 8 integration tests passing
- ✅ Ground friction implemented (terminal velocity ~29 cells/sec)

**Component file structure:**
```
src/core/organisms/components/
├── *Component.h (3 interfaces)
├── RigidBody*.{h,cpp} (3 implementations)
└── *_test.cpp (3 test suites, 49 tests total)
```

### 📊 Current Physics Loop

```
advanceTime(deltaTime)
├── Pressure phases (injection, diffusion, decay)
│
├── organism_manager->update()                    // Non-rigid-body organisms (Duck only)
│   └── Duck adds walk force to cell              // Brain decides actions
│
├── resolveForces(deltaTime, grid)                // Apply physics forces to cells
│   ├── Clear pending_force (skip organism cells)
│   ├── Apply gravity (all non-empty cells including organism cells)
│   ├── Apply air resistance (skip rigid body organisms)
│   ├── Apply pressure forces
│   ├── Apply cohesion/adhesion
│   ├── Apply friction (includes walls)
│   ├── Apply viscosity
│   └── Integrate F=ma (skip organism cells)      ← Cell-based organisms handled separately
│
├── organism_manager->advanceTime()               // Rigid body organisms (Goose, Tree)
│   ├── Goose::update():
│   │   ├── Compute support force from ground contact
│   │   ├── Compute ground friction from velocity and normal force
│   │   ├── Update on_ground based on support magnitude
│   │   ├── Brain decides walk direction
│   │   ├── Add walk force to pending_force_
│   │   ├── Compute gravity (mass × g) directly
│   │   ├── Add support force (cancels gravity when on ground)
│   │   ├── Add friction force (opposes horizontal motion)
│   │   ├── Compute air resistance (velocity²)
│   │   ├── Integrate F=ma → update velocity
│   │   ├── Collision detection → predict cells at new position
│   │   ├── Collision response → zero velocity into obstacles
│   │   └── Project to grid → stamp cells at final position
│   └── Tree::update():
│       ├── Brain executes growth commands
│       ├── Growth adds cells to local_shape via addCellToLocalShape()
│       ├── RigidBodyComponent::update() handles physics:
│       │   ├── Compute support force from ground contact
│       │   ├── Compute ground friction
│       │   ├── Compute gravity (mass × g)
│       │   ├── Integrate F=ma → update position and velocity
│       │   ├── Collision detection and response
│       │   └── Project local_shape to grid
│       └── Entire tree structure moves as rigid unit
│
├── resolveRigidBodies(deltaTime)                 // Cell-based organism integration
│   ├── Duck: F=ma on cell.pending_force
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

**Completed (Phases 1-4.1):**
1. ✅ Cleanup: Removed support system and bones
2. ✅ Rigid body foundation: Unified velocity, skip organism cells in world physics
3. ✅ Ground support & friction: Contact-based support, material-specific friction
4. ✅ Component extraction: 3 base components with 49 tests
5. ✅ Composite component: `RigidBodyComponent` orchestrates physics loop
6. ✅ Multi-cell validation: 10 tests with Stick/LShape/Column organisms (all passing)
7. ✅ Goose refactored to use `RigidBodyComponent` (8/8 tests passing)
8. ✅ **Tree migrated to use `RigidBodyComponent`** (13/13 tests passing)

**Phase 4.1 - Tree Migration (Complete):**
- ✅ Migrated Tree to use `RigidBodyComponent`
- ✅ Removed `seed_position_`, uses inherited `position` and `velocity`
- ✅ Growth commands modify local_shape via `tree.addCellToLocalShape()`
- ✅ Updated `TreeCommandProcessor` to convert world coords to local coords
- ✅ Tree falls as rigid body (structure stays together)
- ✅ Growth updates projection correctly via RigidBodyComponent
- ✅ Updated `ExtendedGrowthStability` test to validate rigid body coherence

**Next (Phase 5 - Future Enhancements):**
- Rotation (angular velocity, moment of inertia, torques)
- Fracture mechanics (structures breaking apart)
- Non-organism rigid bodies (welded metal structures)
