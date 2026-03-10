# Static Load and Region Sleeping

## Overview

This document proposes adding a derived `static_load` field for load-bearing granular materials
plus 8x8 region sleeping. The goal is to let buried dirt and sand carry stable load without
running the full per-cell pressure and force pipeline every frame.

The design is intentionally staged. The first implementation should improve sleeping decisions
without rewriting the entire pressure model or replacing the current per-cell COM transfer system.

## Motivation

The current debug rendering shows a clear pattern in settled dirt piles:

- The free surface and toe remain interesting and dynamic.
- The buried interior carries a stable pressure pattern for long periods.
- The interior often stays visually unchanged unless a disturbance reaches it.

Today the engine still updates the full grid every frame. The main reason is that the solver uses
a single `cell.pressure` field for several different jobs:

- gravity-driven pressure injection,
- blocked-transfer impact pressure,
- pressure diffusion,
- pressure decay,
- pressure-gradient forces,
- debug visualization,
- friction normal-force estimation.

That single field works for the current fully-active solver, but it makes sleeping harder because
"loaded but stable" looks too similar to "recently disturbed."

## Goals

- Sleep buried dirt and sand interiors while preserving granular flow at the surface.
- Keep water behavior conservative and correct.
- Reuse the existing per-cell COM transfer model.
- Use the existing 8x8 bitmap block structure as the first region unit.
- Stage the rollout so behavior changes can be measured and debugged incrementally.

## Non-Goals

- Replacing the cell solver with a coarse 2x2, 4x4, or 8x8 macro-cell solver.
- Building a full stress tensor or continuum soil mechanics model.
- Solving historical stress memory across gravity changes.
- Fixing every cache invalidation bottleneck in the same change.
- Sleeping water regions in the first implementation.

## Current System

### Pressure

Core cell state stores one `pressure` scalar and one `pressure_gradient` vector. The frame loop:

1. injects gravity pressure,
2. adds blocked-transfer pressure,
3. diffuses pressure,
4. decays pressure,
5. applies pressure-gradient forces,
6. moves material using the per-cell COM model.

This pressure field is also read by the friction calculator as part of the contact normal-force
estimate.

### Transport and Debug Rendering

The binary render transport already includes two separate debug fields:

- `DebugCell.pressure_hydro`,
- `DebugCell.pressure_dynamic`.

However, the server currently packs the unified `cell.pressure` value into `pressure_hydro` and
sets `pressure_dynamic` to zero. The UI then reconstructs a single `cell.pressure` value for
debug drawing.

This means the render pipeline already has room for a split representation. The hard part is the
solver semantics, not the transport format.

### Region Structure

`CellBitmap` and `GridOfCells` already use 8x8 blocks. That makes 8x8 the natural first region
unit for sleeping, wake propagation, and future dirty tracking.

### Relevant Code Paths

The current implementation is centered in these files:

- `apps/src/core/Cell.h` and `apps/src/core/Cell.cpp`
- `apps/src/core/World.cpp`
- `apps/src/core/WorldPressureCalculator.cpp`
- `apps/src/core/WorldFrictionCalculator.cpp`
- `apps/src/core/GridOfCells.h` and `apps/src/core/bitmaps/CellBitmap.h`
- `apps/src/core/RenderMessage.h` and `apps/src/core/RenderMessageUtils.h`
- `apps/src/ui/state-machine/states/Disconnected.cpp`
- `apps/src/ui/rendering/CellRenderer.cpp`

## Proposed Architecture

### Core Idea

Separate persistent load-bearing stress from live pressure transport:

- `static_load`: persistent, derived, non-diffusing load for jammed solids.
- `live_pressure`: active pressure field for fluids and transient disturbances.

This is intentionally not a universal "hydrostatic vs dynamic" split on day one. Water still needs
a live pressure field for flow, buoyancy, and equalization. Dirt and sand need a way to carry load
without looking permanently active.

### Terminology and Renames

The current codebase still contains names from an older "hydrostatic vs dynamic pressure" split.
As part of this work, names should be updated to match current semantics even if that changes
serialization.

The guiding rule is:

- use names that describe the actual physical role of the field,
- avoid names that imply a stronger split than the solver actually implements.

Recommended naming direction:

- `Cell.pressure` -> `Cell.live_pressure`
- `Cell.pressure_gradient` -> `Cell.live_pressure_gradient`
- `Cell.static_load` stays `Cell.static_load`
- `PhysicsSettings.pressure_hydrostatic_strength` -> `gravity_pressure_strength`
- `PhysicsSettings.pressure_dynamic_strength` -> `impact_pressure_strength`
- `Material::Properties::hydrostatic_weight` -> `pressure_force_scale`
- `Material::Properties::pressure_injection_weight` -> `gravity_pressure_injection_scale`
- `Material::Properties::dynamic_weight` -> `impact_pressure_scale`
- `DebugCell.pressure_hydro` -> `DebugCell.static_load`
- `DebugCell.pressure_dynamic` -> `DebugCell.live_pressure`

Important constraint:

- Do not rename the current live field to `dynamic_pressure` in phase 1.

That would be misleading because the current live field still represents both calm fluid pressure
and transient disturbance.

### Cell State

Add one field to `Cell`:

```cpp
struct Cell {
    float live_pressure = 0.0f;
    float static_load = 0.0f;
    Vector2f live_pressure_gradient = {};
};
```

Notes:

- In phase 1, the implementation may keep the existing storage names temporarily to minimize churn.
- The design intent is still to migrate toward the terminology above.
- `static_load` is derived from current geometry and gravity.
- `static_load` does not diffuse and does not decay.
- `live_pressure_gradient` remains the gradient of the live pressure field, not of `static_load`.

### Material Policy

Phase 1 should be conservative:

- `DIRT` and `SAND` carry `static_load`.
- `WOOD`, `METAL`, and `WALL` act as support sinks and support boundaries.
- `WATER` and `AIR` never accumulate `static_load`.
- Other solids such as `WOOD`, `METAL`, `ROOT`, and `SEED` can be revisited later once the
  dirt/sand path is stable.

This keeps the first rollout focused on piles of granular material, which is where the expected win
is largest.

### Static Load Semantics

`static_load` answers:

"How much supported compressive load is this cell carrying under the current gravity field and
current local topology?"

It is not a transported fluid quantity. It is not intended to represent impacts or waves. It is
the load-bearing state of jammed granular material.

### Static Load Recompute

`static_load` should be recomputed from current geometry and gravity.

If `abs(gravity)` is below epsilon, all `static_load` values become zero.

The first implementation should use a single directed gravity-aligned load pass rather than an
iterative convergence loop.

For gravity pointing down:

1. Clear `static_load` on all cells.
2. Clear a temporary `incoming_load` grid.
3. Scan rows from top to bottom.
4. For each cell:
   - if it is not `DIRT` or `SAND`, skip it,
   - compute `self_weight = mass * abs(gravity)`,
   - compute `total_load = self_weight + incoming_load[x, y]`,
   - store `cell.static_load = total_load`,
   - find supporting neighbors in the gravity direction:
     - below,
     - below-left,
     - below-right.
5. Distribute `total_load`:
   - if a supported granular cell exists directly below, send 100% downward,
   - else if exactly one diagonal support exists, send 100% to that diagonal,
   - else if both diagonal supports exist, split 50/50,
   - else if the support below is `WOOD`, `METAL`, or `WALL`, terminate the load path there,
   - else the cell is unsupported and transmits no further `static_load`.

For gravity pointing up, reverse the scan direction and support offsets. Horizontal or arbitrary
gravity can be considered later if needed.

This is an approximation, not a full force-chain solver. It will not model arches or complex
lateral jammed structures. That is acceptable for phase 1 because the primary use of `static_load`
is sleeping and debug visualization, not precision contact mechanics.

### Live Pressure Semantics

The existing live pressure field remains the active pressure field for:

- water hydrostatic behavior,
- pressure diffusion,
- blocked-transfer impacts,
- transient disturbances,
- buoyancy and fluid equalization,
- pressure-gradient visualization.

This preserves current water behavior while giving granular interiors a second field that can stay
large without implying the region must remain awake.

### Granular Gravity Reconciliation

The current solver still turns gravity-driven granular loading into live pressure every frame. That
was useful for getting piles to push back against falling material, but it also means buried dirt
looks perpetually disturbed even when its supported load is stable.

The intended long-term split is:

- `static_load` stores steady supported overburden for granular solids,
- `live_pressure` stores fluid pressure and transient disturbance,
- motion comes from imbalance or changing support, not from absolute depth load alone.

This design only pays off for sleeping if steady granular gravity stops behaving like a permanent
disturbance source. In practical terms, the solver needs a material-aware gravity routing policy:

- `WATER` keeps gravity-fed `live_pressure`,
- `DIRT` and `SAND` get gravity-fed `static_load`,
- blocked transfers, impacts, and topology changes still feed `live_pressure`,
- `delta static_load`, not absolute `static_load`, is a wake signal for buried granular regions.

This does not mean granular cells ignore gravity. It means gravity should affect them through:

- direct falling motion for unsupported cells,
- changing support paths and `delta static_load`,
- blocked-transfer or collision disturbance,
- contact and friction capacity once `static_load` is integrated more deeply.

The first data-path patch deliberately did not change this behavior. That keeps the rollout safe,
but it also means the initial implementation cannot achieve quiescent buried dirt under gravity
yet. The design therefore needs an explicit reconciliation phase before sleeping is expected to
work well for loaded granular interiors.

### Region States

Each 8x8 region has one state:

```cpp
enum class RegionState {
    Awake,
    LoadedQuiet,
    Sleeping,
};
```

Suggested metadata:

```cpp
struct RegionMeta {
    RegionState state = RegionState::Awake;
    uint16_t quiet_frames = 0;
    uint32_t last_wake_step = 0;
    uint32_t last_static_load_update = 0;
};
```

## Sleeping Rules

### Eligibility

A region may enter `LoadedQuiet` only if all of the following are true:

- every non-empty cell is `DIRT` or `SAND`,
- the region contains no organism cells,
- the region contains no water cells,
- no cell in the region is adjacent to empty space,
- no cell in the region is adjacent to water,
- `max |velocity| < vel_epsilon`,
- `max |delta live_pressure| < pressure_epsilon`,
- `max |delta static_load| < load_epsilon`,
- no cell COM is near a transfer boundary,
- no blocked-transfer event touched the region this frame.

After the region remains `LoadedQuiet` for `N` frames, it may become `Sleeping`.

### Always-Awake Shell

The first implementation should always keep a boundary shell awake:

- any region touching empty space,
- any region touching water,
- any region with mixed materials,
- any region touched by a recent edit, move, swap, or collision,
- any 1-block halo around the above regions.

This is deliberately conservative. The win comes from sleeping large buried interiors, not from
aggressively sleeping the interesting parts.

### Wake Conditions

Wake a region and its 1-block halo when any of the following occurs:

- material is added, removed, swapped, or moved into the region,
- a blocked transfer is queued in or adjacent to the region,
- `max |delta live_pressure|` exceeds threshold,
- `max |delta static_load|` exceeds threshold,
- a neighboring awake region changes topology,
- gravity changes,
- scenario code edits cells in or adjacent to the region,
- water pressure changes at a dirt/water interface.

## Water Handling

Water is the main reason to avoid an over-simplified global split.

Water behavior depends on a live pressure field. Water does not support shear the same way dirt and
sand do. Because of that:

- water regions stay awake in the first sleeping rollout,
- dirt regions touching water also stay awake in the first sleeping rollout,
- `static_load` is never computed for fluids,
- `live_pressure_gradient` remains the active driver for fluid motion,
- buoyancy and equalization remain on the live pressure path.

This keeps the water path stable while still letting buried dirt interiors sleep.

## Force Integration

### Initial Rollout

The first data-path rollout should not change the pressure-force equations yet.

The data-path patch and first sleeping rollout use `static_load` for:

- debug visualization,
- future instrumentation.

Once sleeping is enabled, it also feeds sleeping decisions.

The live pressure field continues to drive the existing pressure-gradient force path and the
existing friction normal-force estimate.

This keeps behavior close to the current solver while de-risking the structural change.

### Granular Gravity Rollout

Before sleeping buried granular interiors under gravity, the solver needs a narrower change:

- steady gravitational overburden for `DIRT` and `SAND` should primarily update `static_load`,
- water should continue to receive gravity-fed `live_pressure`,
- blocked-transfer and collision energy should continue to feed `live_pressure`,
- granular `live_pressure` should increasingly represent disturbance rather than supported depth
  load.

The design goal is not to eliminate granular motion under gravity. The goal is to stop treating a
stable buried support state as if it were active disturbance every frame.

This phase should still avoid a broad force-law rewrite. It is mainly about deciding what enters
the live-pressure field for each material and what becomes a wake signal.

### Later Physics Integration

Once sleeping is stable, `static_load` can optionally feed solid contact calculations:

- use `static_load` as an additional normal-force term for solid-solid interfaces,
- reduce reliance on live `pressure` for jammed dirt interiors,
- keep live `pressure` dominant for fluids and transient disturbances.

This should be considered a separate follow-on change, not part of the first rollout.

## Debug Rendering

The render transport should be updated to use its existing split fields:

- `static_load` carries derived granular load,
- `live_pressure` carries the active pressure field,
- `live_pressure_gradient` continues to represent the live pressure gradient.

The UI can initially keep the current unified cyan border by displaying the sum:

`display_pressure = static_load + live_pressure`

Later it can add distinct visual modes:

- cyan border for live pressure,
- amber or magenta border for static load,
- region overlay for `LoadedQuiet` and `Sleeping`.

## Implementation Plan

### Cross-Cutting Invariants

- `static_load` is derived state. It may be serialized or transported for debugging, but the engine
  should treat recompute as authoritative after load, resize, or gravity change.
- For granular materials, absolute `static_load` is not itself a disturbance signal. Changes in
  `static_load` are.
- Sleep transitions happen at frame boundaries. Wake requests are recorded immediately, but a region
  should only become `LoadedQuiet` or `Sleeping` after post-frame summarization.
- Wake is conservative and immediate. Sleep is delayed and hysteretic.
- Phase 1 must preserve existing motion when sleeping is disabled.
- Sleeping buried granular interiors under gravity depends on reconciling steady granular gravity
  with `static_load` first.
- The first sleeping rollout should gate force and move work before it tries to gate the live
  pressure solver.

### Suggested Code Organization

Keep orchestration in `World.cpp`, but move the two new responsibilities into dedicated helpers:

- `apps/src/core/WorldStaticLoadCalculator.h/.cpp`
  - owns `static_load` recompute,
  - starts with `recomputeAll(World&)`,
  - can grow a directional dirty recompute later.
- `apps/src/core/WorldRegionActivityTracker.h/.cpp`
  - owns region metadata, wake bookkeeping, region summaries, and active-region masks,
  - stores thresholds and wake-reason counters for debug,
  - exposes helpers such as `wakeRegionAt()`, `summarizeRegions()`, and `buildActiveMask()`.

Recommended storage location:

- Keep `Cell::static_load` in `Cell`.
- Keep previous-frame scratch buffers and region metadata in `World::Impl`.
- Index regions by `GridOfCells::getBlocksX()` and `getBlocksY()`.
- Start with plain region-sized vectors for state and masks. Do not force-fit `CellBitmap` into
  region bookkeeping until the behavior is proven.

### Data Additions

Add these structures or their equivalent:

```cpp
enum class WakeReason : uint8_t {
    None,
    ExternalMutation,
    Move,
    BlockedTransfer,
    NeighborTopologyChanged,
    GravityChanged,
    WaterInterface,
};

struct RegionSummary {
    float max_velocity = 0.0f;
    float max_live_pressure_delta = 0.0f;
    float max_static_load_delta = 0.0f;
    bool has_empty_adjacency = false;
    bool has_water_adjacency = false;
    bool has_mixed_material = false;
    bool has_organism = false;
    bool has_transfer_boundary_com = false;
    bool touched_this_frame = false;
};
```

Additional world-owned scratch data:

- `std::vector<float> previous_live_pressure`,
- `std::vector<float> previous_static_load`,
- `std::vector<RegionMeta> region_meta`,
- `std::vector<RegionSummary> region_summary`,
- `std::vector<uint8_t> active_regions`,
- `std::vector<uint8_t> wake_next_regions`.

The previous-value buffers are important. They let sleeping use `delta live_pressure` and
`delta static_load` without bloating `Cell` with more historical state.

### Frame Update Order

Once `static_load` and region tracking exist, the frame should look like this:

1. Apply any pending external wake requests from edits, scenario code, or setting changes.
2. `ensureGridCacheFresh()`.
3. Recompute `static_load` if phase 1 is active.
4. Build the active-region mask from the previous frame's region state plus current wake requests.
5. Run the live-pressure path using the current material policy.
6. Run force accumulation and move generation using the active-region mask.
7. Process moves and collisions, recording wake reasons for touched regions.
8. Summarize regions from the post-frame cell state.
9. Decide next-frame region states and snapshot previous live/static values.

Important rollout constraint:

- In the first sleeping patch, step 5 stays global, but its granular gravity inputs may already be
  more selective.
- Steps 6 and 7 are the first places that should actually skip work.

That keeps water correctness safer while still attacking the expensive dirt-interior loops.

### Phase 0: Instrumentation and Scaffolding

Primary goal:

- add enough state and debug output to see which regions would sleep before any work is skipped.

Files to touch:

- `apps/src/core/World.h`
- `apps/src/core/World.cpp`
- `apps/src/core/GridOfCells.h`
- `apps/src/core/RenderMessage.h`
- `apps/src/core/RenderMessageUtils.h`
- `apps/src/ui/state-machine/states/Disconnected.cpp`
- `apps/src/ui/rendering/CellRenderer.cpp`

Tasks:

- Add region indexing helpers in `World` or the new region tracker.
- Add `RegionMeta`, `WakeReason`, and debug counters.
- Record region-touch events from direct mutators such as:
  - `addMaterialAtCell()`,
  - `swapCells()`,
  - `replaceMaterialAtCell()`,
  - `clearCellAtPosition()`.
- Add a debug overlay for `Awake`, `LoadedQuiet`, and `Sleeping`.
- Update debug transport so the UI can display split values even before sleeping is enabled.

Acceptance criteria:

- the UI can show both `static_load` and live pressure separately,
- wake reasons can be logged or inspected,
- no physics behavior changes yet.

### Phase 1a: Static Load Data Path

Primary goal:

- compute and transport `static_load` every frame without changing force laws.

Files to touch:

- `apps/src/core/Cell.h`
- `apps/src/core/Cell.cpp`
- `apps/src/core/World.h`
- `apps/src/core/World.cpp`
- `apps/src/core/WorldStaticLoadCalculator.h` (new)
- `apps/src/core/WorldStaticLoadCalculator.cpp` (new)
- `apps/src/core/RenderMessage.h`
- `apps/src/core/RenderMessageUtils.h`
- `apps/src/ui/state-machine/states/Disconnected.cpp`
- `apps/src/ui/rendering/CellRenderer.cpp`
- `apps/src/tests/Cell_serialization_test.cpp`
- new tests for static-load recompute

Tasks:

- Add `Cell::static_load`.
- Ensure `clear()`, `replaceMaterial()`, and the air transition path zero `static_load`.
- Recompute `static_load` globally each frame using the directed gravity-aligned load pass.
- Treat `WOOD`, `METAL`, and `WALL` as supports and termination sinks during that recompute.
- Pack `static_load` and live pressure separately into debug transport.
- Recompute `static_load` after world deserialization instead of trusting persisted values.
- Keep all existing force calculations on the current live-pressure path.

Acceptance criteria:

- debug rendering shows stable `static_load` in buried dirt and sand,
- water scenes still behave the same,
- disabling sleeping yields motion equivalent to the current engine.

### Phase 1b: Terminology Sweep

Primary goal:

- make names match semantics in one mechanical pass after the data path exists.

Files likely touched:

- `apps/src/core/Cell.h`
- `apps/src/core/Cell.cpp`
- `apps/src/core/MaterialType.h`
- `apps/src/core/MaterialType.cpp`
- `apps/src/core/PhysicsSettings.h`
- `apps/src/core/PhysicsSettings.cpp`
- `apps/src/core/World.cpp`
- `apps/src/core/WorldPressureCalculator.cpp`
- `apps/src/core/WorldFrictionCalculator.cpp`
- `apps/src/core/RenderMessage.h`
- `apps/src/core/RenderMessageUtils.h`
- `apps/src/ui/controls/PhysicsControlHelpers.cpp`
- `apps/src/server/states/SimRunning.cpp`
- `apps/src/ui/state-machine/network/MessageParser.cpp`
- scenarios, docs, and tests that reference old names

Tasks:

- Rename pressure fields and settings according to the terminology section above.
- Update UI controls, server setters, tests, and scenario defaults.
- Update `GridMechanics.md` and any remaining references to the older hydrostatic/dynamic split.

Implementation note:

- this should be a mostly mechanical patch or commit, separated from the sleeping behavior change
  if practical.

### Phase 1c: Granular Gravity Reconciliation

Primary goal:

- stop steady granular overburden from behaving like perpetual live disturbance.

Files likely touched:

- `apps/src/core/World.cpp`
- `apps/src/core/WorldPressureCalculator.cpp`
- `apps/src/core/WorldStaticLoadCalculator.cpp`
- `apps/src/core/WorldFrictionCalculator.cpp`
- `apps/src/core/MaterialType.h`
- `apps/src/core/MaterialType.cpp`
- `apps/src/core/tests/WorldRegionSleepingBehavior_test.cpp`
- water and pressure regression tests

Tasks:

- Define gravity routing by material:
  - `WATER` keeps gravity-fed `live_pressure`,
  - `DIRT` and `SAND` route steady overburden into `static_load`,
  - blocked transfers and collisions still feed `live_pressure`.
- Revisit granular gravity-pressure injection so buried supported dirt is not constantly re-awakened
  by steady load alone.
- Use the region behavior tests to verify that gravity-on buried dirt can become quiet while
  gravity-off still clears `static_load`.
- Keep the change narrow enough that water equalization and buoyancy behavior remain close to the
  current solver.
- Defer any larger contact-force rewrite unless the quieter live-pressure path still is not enough.

Acceptance criteria:

- a buried dirt core can reach `LoadedQuiet` or `Sleeping` under constant gravity,
- free-surface dirt still falls and avalanches,
- support removal wakes the affected interior through disturbance or `delta static_load`,
- water scenarios remain visually close to current behavior.

### Current Findings from Buried-Core Diagnostics

The initial instrumentation and behavior-test work changed the understanding of what is blocking
sleeping today.

What is now implemented and measured:

- `static_load` is computed and transported separately from live pressure.
- Region activity overlays and behavior tests exist for deep dirt piles.
- Granular gravity-pressure injection has been narrowed for `DIRT` and `SAND`.
- Supported buried granular cells can skip direct gravity in the current experiment.

What the buried-core diagnostics showed:

- These changes reduce pressure churn and reduce buried-region velocity, but they do not by
  themselves make a buried core leave `Awake`.
- In a pure buried dirt region, `static_load` is stable and `delta live_pressure` is small.
- Gravity skipping is active across the whole pure buried region.
- Many cells still sit at `com.y ~= 1.0` and keep generating queued move events.

The most important finding is that these queued move events are mostly not real transport:

- the pure buried region shows repeated generated and received move bookkeeping,
- but zero successful transfers,
- and zero blocked-transfer amounts in the measured frames.

This means the buried-core blocker is currently better described as transfer-threshold thrash than
as meaningful cross-cell motion. The region tracker is therefore too conservative when it treats
"touched by a move attempt" as proof of real region activity.

This finding changes the near-term strategy:

- production sleeping should not key primarily on queued move attempts,
- sleeping should instead key on real boundary effects such as successful transfers and swaps,
- internal velocity and pressure/load deltas should still be used as conservative guards,
- blocked transfers should remain a debug signal first and only become a wake signal later if they
  prove necessary with a threshold.

An observer-only test heuristic already validates this direction:

- if a buried region sees no successful transfers for several frames,
- and its mean/max velocity and pressure/load deltas stay under conservative thresholds,
- the test can identify a buried dirt region as a sleep candidate under gravity even while queued
  move noise continues.

### Revised Working Theory

The latest buried-core investigation suggests that sleeping alone is not the full answer.

`static_load` currently helps with observability and with skipping direct gravity on supported
granular cells, but that is not enough by itself. If the awake solver still treats tiny
gravity-aligned COM boundary crossings as material moves, buried dirt keeps chattering even when
its support state is stable.

The current working theory is:

- `static_load` should participate directly in awake granular mechanics, not only in sleeping
  decisions,
- the first mechanical use should be supported compression contact handling for buried granular
  cells,
- friction capacity still matters, but it is a follow-on once compression contacts are being
  resolved coherently,
- swap-resistance tuning is a later follow-on and should not block the first settling pass.

In practical terms, the next solver pass should try this:

- when a supported granular cell with meaningful transmitted `static_load` crosses the boundary in
  the gravity direction into a non-empty supporting neighbor at low normal speed, interpret that as
  compression rather than transport,
- in that compression case, keep a queued contact event so the solver can still resolve the
  interaction, but do not treat it as transport,
- use a dedicated compression response that removes or heavily damps inward relative normal motion,
  clamps COM just inside the crossed boundary, and preserves tangential motion,
- let friction and viscosity dissipate the remaining tangential drift over time.

This is intentionally narrower than adding a new global support force. The goal is not to invent a
second anti-gravity system. The goal is to stop treating stable buried compression as if it were
new transport every frame.

If this theory is right, the practical sequence becomes:

- resolve buried low-energy compression as contact, not transport,
- stop counting those compression contacts as wake-worthy transport,
- add load-aware friction after compression handling is stable,
- keep wake logic conservative and keyed to real transfers,
- then sleep regions only after the awake solver can actually settle.

### Updated Findings From Staged Force Isolation

The staged force-isolation pass changed the picture from "maybe sleeping criteria are too strict" to
"there were several distinct physics causes, and they compound if left unresolved."

The current understanding is:

1. There was a gravity-bootstrap timing bug.

   Supported buried granular cells were already able to discover a support path on the first frame,
   but `static_load` was still stale at that moment, so they took one frame of gravity before the
   later `static_load` recompute caught up. That one-frame impulse seeded the characteristic
   residual downward drift seen in the early gravity-only diagnostics.

   The fix is to make `static_load` valid before gravity is applied, not just after moves.

2. Granular compaction was creating a persistent diffusive live-pressure field.

   The remaining `gravity + pressure` drift was not coming from hydrostatic pressure injection and
   not from blocked-transfer pressure. It was coming from `pressure_from_excess` generated during
   granular compaction near the pile surface and shoulders, then diffusing inward and being treated
   as a real pressure-gradient force inside already-supported buried dirt.

   Suppressing this excess-move pressure for granular compaction into already-supporting
   granular/solid targets removed that pressure-side buried drift in the isolated diagnostics.

3. Current "viscosity" is really same-material momentum diffusion, not pure damping.

   In `viscosity_only`, the pure buried region had no pressure source, no meaningful transfer
   noise, and gravity was already skipped. The remaining motion came from neighbor-velocity
   averaging at the buried-region edge: active slope dirt was diffusing downward velocity into
   nearby supported buried dirt.

   That means the current viscosity model is acceptable for flowing / fluidized material, but it is
   the wrong behavior for supported load-bearing granular cells. Buried jammed dirt should not keep
   inheriting slope motion just because it shares a material type with moving dirt nearby.

4. The narrow load-aware viscosity gate is currently the strongest working result.

   A first-pass rule of "do not apply same-material viscosity coupling to supported buried granular
   cells that already skip gravity" cleaned up the `viscosity_only` profile, cleaned up
   `full_minus_pressure`, cleaned up `full` in the force-isolation sweep, and made the original
   disabled buried-pile sleeping test pass under the real region tracker.

This does not mean the broader mechanics are finished. It does mean the branch now has a concrete,
evidence-backed working direction:

- `static_load` must be valid before force application,
- supported low-energy buried compression should be resolved as contact, not transport,
- supported granular compaction should not create a fake diffusive live-pressure field,
- supported load-bearing granular cells should not participate in same-material viscosity
  diffusion,
- only after those mechanics are quiet should scheduler sleep policy be tightened.

The viscosity result is especially important because it argues against the blunt fix of simply
setting granular viscosity to zero everywhere. The observed problem was not "all granular viscosity
is bad." The problem was "neighbor-velocity diffusion should not apply unchanged to supported
jammed granular material."

### First Implementation Sketch

The current disabled buried-pile diagnostics give a useful starting point:

- buried cells in the failing regions already carry large `static_load`,
- direct gravity is already skipped for those cells,
- the same cells still spend many frames with `com.y` at or near the gravity-aligned boundary,
- generated and received move counts stay high while successful transfer counts stay at zero.

The latest passive instrumentation adds two important clarifications:

- in the pure buried region, the queued move noise is overwhelmingly vertical rather than lateral,
- the candidate predicate for "supported low-speed compression" matches the noisy generated
  downward moves closely.

That means the first implementation should focus on reclassifying supported compressive boundary
crossings, not on adding another global support force and not on retuning lateral flow first.

#### Step 1: Compression Contact In The Move/Collision Path

The first attempted implementation simply suppressed these crossings in
`World::computeMaterialMoves()` before queuing a move. That did not work.

Why it failed:

- the cells were still acquiring nonzero velocity earlier in the frame from pressure, cohesion,
  friction, and viscosity,
- the move/collision path is currently where the solver performs the actual contact response for
  those boundary crossings,
- removing the queued event too early removed not only the fake transport bookkeeping, but also the
  only place the current solver was resolving that contact at all.

So the replacement should not be "drop the move." It should be "keep the event, but change its
physics meaning."

The right hook is still around `World::computeMaterialMoves()` and
`WorldCollisionCalculator::createCollisionAwareMove()`, but the output should be a dedicated
compression-contact move type rather than a suppressed move.

Add a helper predicate for a compressive-contact crossing:

- source cell is load-bearing granular,
- crossing direction matches gravity,
- source cell has a supported granular path in the gravity direction,
- source cell carries transmitted load, for example `static_load > self_weight + epsilon`,
- target cell is not empty,
- target cell is either another load-bearing granular cell or a support sink such as wall, wood, or
  metal,
- target cell has little or no free capacity,
- inward normal speed is positive but still in a low-speed settling band, not a real impact.

If the predicate is true:

- queue a dedicated `CompressionContact` event,
- do not treat it as successful transport, blocked transfer, or swap input,
- process it in collision handling with a dedicated `handleCompressionContact()` path,
- record dedicated debug counters for candidate detection and compression-contact handling.

Important: this is not a reflection path. Reflection re-injects bounce. The response should be a
plastic or highly damped compression contact: "the contact absorbed inward normal motion" rather
than "the cell bounced."

The expected contact response is:

- decompose source and target velocities into normal and tangential components along the crossed
  boundary,
- remove or heavily damp the relative inward normal component,
- for rigid support sinks, clamp the source normal component toward zero,
- for loaded granular-granular contacts, converge both cells toward a shared heavily damped normal
  velocity rather than a reflected one,
- preserve tangential motion,
- clamp COM just inside the crossed boundary to prevent immediate retriggering,
- do not generate blocked-transfer pressure for these low-energy supported compression contacts.

This should also happen before region move bookkeeping is interpreted as real activity, so a
compression contact does not become a fake wake signal.

#### Step 2: Bounded Static-Load Friction Boost

`static_load` should still strengthen granular friction, but this is now clearly the second step,
not the first one.

The instrumentation suggests the buried-core failure is dominated by vertical compression chatter.
So friction matters mainly after that contact is being resolved coherently.

When friction is integrated, the first pass should be bounded rather than using raw `static_load`
as a one-to-one replacement for contact normal force.

The current friction solver applies explicit force accumulation each frame. A direct substitution of
large buried `static_load` values into the normal-force term is likely to overdrive tangential
forces and create solver bursts before the move clamp has a chance to quiet the pile.

The safer first pass is:

- keep the existing pressure-difference contribution,
- keep the existing local weight floor for vertical contacts,
- add a capped or scaled confining-load boost for granular contacts,
- only apply that boost for granular-granular and granular-solid interfaces,
- tune the boost so it increases stickiness for buried dirt without letting friction create large
  new lateral impulses.

Conceptually, the confining term should be "more load means more shear resistance," not "apply the
entire overburden as a free tangential-force budget."

#### Step 3: Sleeping After The Awake Solver Settles

Once Step 1 and then the bounded Step 2 friction boost reduce buried churn:

- keep region wake logic focused on successful transfers, swaps, meaningful support changes, and
  explicit compression-contact events only if they prove necessary later,
- do not treat compression contacts as transport,
- then allow buried regions to sleep when velocity, transfer, and load-delta thresholds are quiet.

This staging matters. The awake solver should become physically quieter before sleeping logic is
asked to hide the remaining noise.

### Phase 2: Conservative Sleeping

Primary goal:

- sleep only buried dirt and sand interiors while leaving water and boundaries fully active.

Files to touch:

- `apps/src/core/World.h`
- `apps/src/core/World.cpp`
- `apps/src/core/WorldRegionActivityTracker.h` (new)
- `apps/src/core/WorldRegionActivityTracker.cpp` (new)
- `apps/src/core/WorldPressureCalculator.cpp`
- `apps/src/core/WorldCollisionCalculator.cpp`
- `apps/src/core/WorldFrictionCalculator.cpp`
- `apps/src/core/GridOfCells.h`
- tests and debug overlay code

Tasks:

- Summarize each 8x8 region after every frame.
- Build the always-awake shell from:
  - empty adjacency,
  - water adjacency,
  - mixed materials,
  - organism occupancy.
- Add wake hooks for:
  - direct cell edits,
  - blocked transfers,
  - material moves,
  - gravity changes,
  - neighboring topology changes.
- Add a low-speed gravity-aligned compression-contact path for supported loaded granular cells:
  - when the target in the gravity direction is already a supporting solid or loaded granular cell,
  - and the attempted crossing is a small compressive event rather than a real rearrangement,
  - queue a dedicated compression contact instead of treating it as transfer or reflection,
  - damp relative inward normal motion, clamp COM inside the crossed boundary, and preserve
    tangential motion.
- Keep compression contacts out of blocked-transfer pressure generation and out of transport-based
  wake bookkeeping.
- Suppress excess-move pressure for granular compaction into already-supporting granular/solid
  targets so buried support compression does not pump a fake live-pressure field into the pile.
- Gate same-material viscosity coupling for supported load-bearing granular cells:
  - the first pass can be a hard gate tied to the same support/load state already used for gravity
    skipping,
  - later tuning may replace that hard gate with a smoother load-aware "fluidization factor" if
    flowing-surface behavior needs a softer transition.
- Feed `static_load` into granular friction/contact normal-force estimation only after compression
  contact handling is stable, and keep that first pass bounded.
- Separate queued move attempts from real boundary activity.
- In the first sleeping heuristic, treat boundary activity primarily as:
  - successful transfers,
  - swaps.
- Do not let zero-effect queued move attempts or compression contacts by themselves keep a buried
  region awake.
- Preserve tangential motion in the compression contact so friction remains the primary damper for
  shear and sliding.
- Defer swap-resistance tuning for now. The current swap system is heuristic and may be replaced by
  a better contact-displacement model later.
- Keep blocked-transfer counts in debug output during the first heuristic pass.
- Only promote blocked transfers into wake logic later if an amount or energy threshold proves
  necessary.
- Gate only these expensive paths first:
  - pressure-force accumulation,
  - friction/viscosity force accumulation,
  - force resolution,
  - move generation.
- Keep live pressure injection, diffusion, and decay global in this first sleeping pass.
- Expand awake regions by a conservative 1-block halo before gating loops.

Performance note:

- Avoid doing block-coordinate math inside the hottest inner loops if possible.
- Precompute a cell-space or region-space active mask once per frame, then branch cheaply.

Acceptance criteria:

- compression-contact handling reduces buried-core move churn under gravity without increasing
  buried-core velocity,
- pre-force `static_load` recompute removes the one-frame buried gravity bootstrap impulse,
- suppressing granular excess-move pressure removes buried `gravity + pressure` drift in the
  isolated diagnostics,
- gating viscosity for supported buried granular cells removes edge-driven momentum seepage from
  `viscosity_only` and still allows the buried deep-pile test to pass under the real region
  tracker,
- bounded load-aware friction further improves settling once added,
- observer-only tests prove that buried dirt can qualify as transfer-quiet even with queued move
  noise,
- the buried interior of a stable dirt pile reaches `Sleeping`,
- the free surface, toe, and dirt-water interface remain awake,
- tapping, digging, or collapsing a pile wakes the interior quickly,
- water scenarios such as DamBreak remain visually unchanged.

### Phase 3: Directional Dirty Recompute and Broader Gating

Primary goal:

- reduce the global recompute work once the conservative sleeping path is trustworthy.

Tasks:

- Add dirty tracking for `static_load` recompute.
- Start with a coarse directional invalidation rule, not a tiny local halo.
- When a cell changes, mark:
  - the touched region,
  - gravity-upstream regions that may have lost support,
  - gravity-downstream regions that may receive a different transmitted load.
- Only after that is stable, consider limiting live-pressure work for sleeping granular interiors
  that are far from water and wake boundaries.
- Revisit whether global `GridOfCells` rebuilds can be reduced or partially regionalized.

Important constraint:

- `static_load` propagation is directional. A plain dirty-region-plus-halo update is probably too
  optimistic because support changes can affect a long vertical column.

### Phase 4: Optional Physics Integration

Primary goal:

- decide whether `static_load` should influence contact forces after sleeping is already useful.

Tasks:

- Feed `static_load` into solid contact normal-force estimation.
- Revisit whether jammed dirt should respond less to live pressure.
- Extend `static_load` accumulation beyond dirt and sand to `WOOD`, `METAL`, `ROOT`, and `SEED`
  only if that proves useful for debugging or contact modeling.

Success criterion:

- any force-law change here should be measurable and justified independently of sleeping.

## Validation

Use these scenarios to validate correctness and value:

- Sandbox dirt pile with debug draw enabled:
  - buried interior should become `LoadedQuiet` then `Sleeping`,
  - free surface and toe should remain awake.
- Sandbox dirt pile under constant gravity before sleeping is enabled:
  - buried interior should stop showing persistent movement-driven wakeups,
  - `delta static_load` should remain near zero away from disturbances.
- Sandbox dirt pile with the current viscosity gate:
  - buried supported regions should not inherit downslope velocity from nearby moving dirt,
  - flowing surface dirt should still look plausibly loose and avalanche when disturbed.
- Tap or dig into a pile:
  - wake should propagate inward from the disturbance.
- Dirt next to water:
  - interface should remain awake,
  - water equalization should remain unchanged.
- DamBreak:
  - behavior should match current dynamics.
- Gravity disabled:
  - `static_load` should recompute to zero.

Useful metrics:

- sleeping region count,
- wake count by reason,
- per-frame time in pressure, friction, and move generation,
- number of regions skipped,
- diff videos and debug screenshots before and after.

Recommended automated coverage:

- `WorldStaticLoadCalculator` unit tests:
  - vertical dirt stack,
  - diagonal support,
  - support termination into `WOOD`, `METAL`, and `WALL`,
  - zero gravity clears all `static_load`,
  - water cells do not accumulate `static_load`.
- render transport tests:
  - `static_load` and live pressure survive pack/unpack separately.
- regression tests:
  - `Buoyancy_test.cpp`,
  - `DiagonalWaterLeveling_test.cpp`,
  - `DuckBuoyancy_test.cpp`.
- region sleep behavior tests:
  - gravity-on buried dirt can quiet,
  - gravity-off clears `static_load`,
  - transfer-quiet observer heuristic finds a buried candidate under gravity,
  - a pure buried region can qualify despite queued move noise,
  - dirt next to water stays conservative,
  - toe-disturbance wake propagation remains local first.
- manual scenario captures:
  - sandbox dirt pile at rest,
  - digging into buried dirt,
  - dirt next to water,
  - DamBreak.

## Risks and Open Questions

- The first `static_load` solver is approximate and may miss arches or force chains.
- Conservative wake rules may reduce the initial speedup.
- Aggressive thresholds may over-stabilize steep slopes and suppress avalanches.
- Reconciling granular gravity too aggressively could remove useful disturbance at the surface if
  the material policy is not tuned carefully.
- The current COM transfer model can generate persistent zero-effect move churn near `com ~= 1.0`;
  sleeping logic must not confuse that bookkeeping with real transport.
- The current viscosity gate is intentionally narrow and somewhat blunt. It may need to evolve into
  a softer load-aware fluidization rule if the boundary between flowing and supported granular
  material looks too abrupt in sandbox scenarios.
- A transfer-quiet scheduler heuristic may be useful even if the underlying mechanics are still
  somewhat jittery, but wake thresholds will need careful tuning to avoid visible pops.
- Global `GridOfCells` cache rebuilds still limit maximum performance until dirty tracking is added.
- The UI debug path currently assumes one unified pressure value and must be updated carefully.
- The existing pressure settings and material-property names still reflect older terminology and
  should be cleaned up as part of implementation.

## Recommendation

Proceed with the staged design above:

1. add `static_load` as derived state for dirt and sand,
2. make sure `static_load` is recomputed before gravity/force application so supported buried cells
   do not get a one-frame gravity impulse,
3. use it for debug and observability first, but also for supported low-energy compression-contact
   classification,
4. treat supported low-energy buried crossings as compression contacts, not transport, and resolve
   them in the move/collision path rather than suppressing them early,
5. suppress excess-move pressure for already-supported granular compaction so buried support
   compression does not generate a fake live-pressure field,
6. gate same-material viscosity coupling for supported load-bearing granular cells so buried jammed
   dirt does not inherit slope motion from nearby flowing dirt,
7. validate flowing-surface and avalanche behavior with that viscosity gate before redesigning
   granular viscosity more broadly,
8. add bounded `static_load` friction once the above mechanics are stable,
9. treat successful boundary crossings, not queued move attempts, as the first sleep-heuristic
   definition of real region activity,
10. keep water on the current live pressure path,
11. sleep only buried granular interiors in the first implementation.

This gives the expected win for dirt piles without forcing a full pressure-model rewrite in one
step, and it keeps the next work focused on validating the new load-aware viscosity boundary rather
than jumping straight to scheduler tuning.
