# Rigid Body Design for Grid-Based Physics

## The Problem

Currently, rigid materials (WOOD, METAL) are represented as individual particles with cohesion forces. This causes issues:
- Horizontal structures fall apart (each cell moves independently)
- High cohesion creates COM drift and pressure waves
- No concept of structural integrity

**Goal:** Enable organism structures (trees) to move as rigid units while still participating in grid-based physics.

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
│   └── Apply F=ma to each cell → velocity
├── Velocity limiting
├── updateTransfers() → compute material moves
├── processMaterialMoves() → execute moves
└── TreeManager::update()
```

Modified loop:
```
advanceTime(deltaTime)
├── Pressure phases
├── identifyRigidStructures() → structures, rigid_cell_set  [NEW]
├── resolveForces(rigid_cell_set)  [MODIFIED]
│   ├── Accumulate forces (all cells)
│   └── Apply F=ma only to NON-rigid cells
├── resolveRigidBodies(structures)  [NEW]
│   ├── For each structure: gather forces, F=ma, set unified velocity
├── Velocity limiting
├── updateTransfers()
├── processMaterialMoves()
└── TreeManager::update()
```

### Phase 3: Movement & Collisions

**3.1 Structure Movement**
- When structure velocity moves COMs across cell boundaries
- Identify "leading edge" cells that will enter new grid positions
- Calculate resistance from destination cells
- Either displace particles or reflect/stop structure

**3.2 Structure-Particle Interaction**
- Structure pushes through fluids (water parts around it)
- Structure displaces loose particles (dirt, sand get pushed aside)
- High-resistance materials slow/stop structure

### Phase 4: Future (Not This Sprint)

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

### Tests to Update
- `src/core/organisms/tests/CantileverSupport_test.cpp` - remove or repurpose
- Various tests that check `has_any_support`

## Work Sequence

1. **Remove support system** (cleanup)
2. **Remove is_rigid** (cleanup)
3. **Update structure identification** (organism_id based)
4. **Add applyUnifiedVelocity** + tests
5. **Integrate into World::advanceTime**
6. **Integration tests** (falling, floating, etc.)
7. **Structure movement/collisions** (leading edge handling)
