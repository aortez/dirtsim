# Water Simulation Rewrite: MAC Projection + Separate Water Layer

## Context

The current water implementation is entangled with the per-cell COM transfer solver.
It has three core problems:

1. It injects energy during fragmentation and other collision paths.
2. It tends to generate energy even without explicit disturbances.
3. It equalizes extremely slowly through openings because motion is quantized into per-cell bounces.

This document proposes a fresh water system that is designed for the quantized grid world instead
of trying to tune the existing COM transfer behavior.

## Goals

- **Fast equalization:** A 50x50 world should equalize water across openings in a reasonable time.
- **Stable rest:** A resting pool should converge to near-zero velocity without perpetual jitter.
- **Mass conservation:** Water volume is conserved (within small numerical error).
- **Inertia + sloshing:** Support waves and momentum in a coarse but believable way.
- **Solid displacement:** When dirt/solids enter water, water is displaced (not deleted).
- **Pluggable fidelity:** Support swapping water solvers (cheap ↔ accurate) without rewriting `World`.

## Status (WIP)

This design is partially implemented and is **still under active iteration**. Expect behavior to be
visibly “crazy” in some Sandbox situations until the solver is tuned and made more physically
consistent.

### Implemented (as of 2026-03-18)

- `WaterSimMode::MacProjection` is the **default** water simulation mode.
- Water is tracked in a separate-layer `waterVolume[x,y] ∈ [0,1]` (canonical state).
- Scenario water spawning uses `World::addMaterialAtCell(..., Water, ...)` which writes into
  `waterVolume` when MAC mode is active.
- Rendering: server overlays water volume onto **Basic** render messages (water tint + fill) for air
  cells.
- `StateGet` includes `WorldData.water_volume` so clients/tools can inspect the volume field.
- Tests exist for: opening flow equalization, 50×50 quadrant equalization, resting pool stability,
  and basic solid displacement volume conservation.

### Current Migration State (as of 2026-03-21)

- The default mode flipped globally before the legacy `Cell.material_type == Water` codepaths were
  fully retired.
- Some scenarios and helpers still assume water is stored directly in `Cell`, so compatibility work
  is still incomplete.
- Scenario setup and reset code must clear or repopulate the separate `waterVolume` layer
  explicitly; clearing only the cell grid is not sufficient when a `World` instance is reused.
- Streamed **Debug** render still does not carry the separate water layer; only **Basic** render
  gets the server-side water overlay, while `StateGet` exposes `water_volume` directly.

### Known Issues / Needs Work

- Water motion can be unstable/jittery in Sandbox; wall pile-ups and “floating” artifacts can
  happen.
- Volume advection is currently a simple neighbor-transfer scheme with heuristic scaling; it needs a
  more principled advection method (and better CFL behavior) to look sane.
- Boundary conditions and free-surface behavior are approximate; the pressure projection is a fixed
  iteration-count solve over a small fluid “shell”.
- Two-way coupling (buoyancy/drag on solids) is minimal/experimental and not tuned.

## Non-Goals (initially)

- Perfect turbulence / high-frequency detail.
- Full two-way coupling with *all* materials on day one.
- Reusing the COM transfer code path for water movement.

## Key Architectural Decision

**Water is simulated as a separate layer from the cell-material solver.**

- The cell grid remains responsible for solids (and other non-water materials).
- Water is tracked in a dedicated water simulation state:
  - `waterVolume[x,y]` in `[0, 1]` per cell (canonical).
  - A velocity field (MAC faces) for inertia and sloshing.

Rendering and serialization can be derived from `waterVolume` instead of storing `Material::Water`
directly in `Cell` (migration is staged below).

## Proposed Solver: 2D MAC Projection

Use a classic “Stable Fluids” style pipeline adapted to the grid constraints:

### State

- **VOF-like volume fraction:** `waterVolume[x,y]` ∈ [0,1].
- **MAC velocities:**
  - `u[x+1/2, y]` horizontal face velocity.
  - `v[x, y+1/2]` vertical face velocity.

### Step Pipeline (per frame)

1. **Build obstacle mask** from the cell grid (walls/solids block flow).
2. **Apply external forces** (gravity, optional user impulses) to face velocities.
3. **Advect velocities** (semi-Lagrangian is acceptable at this scale).
4. **Project to incompressible:**
   - Compute divergence of the MAC field in fluid cells.
   - Solve Poisson for pressure `p` (Jacobi/Gauss-Seidel iterations; bounded iteration count).
   - Subtract ∇p from velocities (clamp solid faces to 0 / enforce boundary conditions).
5. **Advect water volume** using the post-projection velocity field.
6. **Stabilize rest:**
   - Apply a small viscosity / damping term.
   - Clamp tiny velocities to 0 to avoid endless micro-motion.
   - Optional “water sleep” state when kinetic energy stays below a threshold for N frames.

This addresses the quantization issue by allowing water to move as a continuous field over a grid
instead of only moving when COM crosses a cell boundary.

## Coupling With Solids (Staged)

### Displacement (required)

When a solid cell appears in a location that had water volume:

- Reduce `waterVolume` for that cell by the new solid occupancy.
- Push the displaced volume into neighbor cells via a local redistribution pass (bounded work).
- If redistribution cannot fit (fully packed region), allow a small “pressure source” impulse that
  drives water out over subsequent frames.

### One-way boundaries (initially)

Initially, solids act as static obstacles for water. Two-way coupling (buoyancy/drag on solids)
can be added later once water-only behavior is stable.

## Pluggable Fidelity

Introduce a runtime-selectable mode:

- `WaterSimMode::LegacyCell`: current behavior (water as `Cell.material_type == Water`).
- `WaterSimMode::MacProjection`: new separate-layer solver.
- Future: `WaterSimMode::Heightfield` (cheap surface flow), `WaterSimMode::Particles` (SPH), etc.

The key is keeping a stable boundary between `World` and the water subsystem:

- Water solver consumes:
  - world dimensions,
  - an obstacle mask derived from cells,
  - coupling events (solid placed/removed, impulses).
- Water solver produces:
  - water volume + velocity state,
  - a derived render/serialization view.

## Incremental Rollout Plan

### Phase 0 (Scaffold)

- Add `WaterSimMode` to `PhysicsSettings`.
- Add `IWaterSim` + `WaterSimSystem` plumbing.
- Add a placeholder `MacProjectionWaterSim` implementation.

### Phase 1 (Water-only MAC in a sandbox)

- Create a water-only scenario that initializes `waterVolume` directly.
- Render water from the water layer (no `Material::Water` cells involved).
- Add tests for:
  - conservation,
  - equalization through a 1-cell opening,
  - resting pool stability.

Historical note:

- The implementation has moved past this narrow sandbox-only phase in one important sense:
  `WaterSimMode::MacProjection` is now the global default.
- The migration is still incomplete, though, so parts of the codebase continue to behave as if this
  phase and the later legacy-retirement phase are overlapping.

### Phase 2 (Cell coupling + displacement)

- When solids are placed/removed, update obstacle mask and displace water.
- Preserve mass during edits and fragmentation events.

### Phase 3 (Retire legacy water cells)

- Remove `Material::Water` from the COM transfer physics path.
- Keep `Material::Water` only as a derived view for UI/serialization if still useful.

## Testing Strategy

- **Unit tests:** deterministic micro-worlds (3x6 equalization, 50x50 quadrant equalization).
- **Stability tests:** sealed tank with flat surface converges to near-zero motion.
- **Regression harness:** compare volume conservation and convergence time across modes.

## Current Serialization Rule

- `waterVolume` is already serialized for transport and save/load in the current implementation.
- Backwards compatibility is **explicitly not supported** and is not planned.
- The format may change at any time without version tags, migrations, or compatibility shims.

## Open Questions

- How to best represent “wetness” or mixing if dirt absorbs water (separate system vs coupling).
- Whether to support negative pressure / cavitation-like artifacts or clamp for stability.
