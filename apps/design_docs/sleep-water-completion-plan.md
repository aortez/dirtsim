# Sleep + Water Completion Plan

## Purpose

This document is the working delivery plan for finishing region sleeping and the MAC water
transition together.

It is intentionally different from the durable design docs:

- `static-load-and-region-sleeping.md` explains the sleeping/static-load design.
- `water-sim-mac-projection.md` explains the separate-layer MAC water design.

This document focuses on current branch status, sequencing, acceptance criteria, and deferred work.

## Related Docs

- `apps/design_docs/static-load-and-region-sleeping.md`
- `apps/design_docs/water-sim-mac-projection.md`

## Terminology

- `tracked-only`: region sleep state is computed and exposed for debug/analysis, but the runtime does
  not skip work because of it.
- `sleep-enforced`: region sleep state is allowed to gate physics work in selected hot loops.

## Near-Term Priority Order

1. Finish MAC water ownership and replace the remaining behavior-coupled legacy-water paths.
2. Remove legacy bulk-water APIs and call patterns once migrated replacements exist.
3. Add MAC-based quiet metrics and allow calm pool interiors to sleep.
4. Gate MAC water work by active regions if profiling shows it is still needed.
5. Add an optional transient spray/droplet layer.

## Current Branch Status

### Sleeping

- `WorldRegionActivityTracker` is wired into the frame loop and computes region state, wake reasons,
  and active masks.
- External edits, blocked transfers, gravity changes, and material moves already feed the tracker.
- Region debug overlay plumbing exists and is visible in the UI.
- Selected dry-region hot loops now enforce sleeping for conservative dry interiors.
- Water regions and dirt/water boundary regions remain `tracked-only`.
- Static-load work, MAC-water work, and some dry-region processing are still not gated by the sleep
  mask.

### Water

- `WaterSimMode::MacProjection` is the global default.
- Separate-layer `waterVolume` is the canonical bulk-water state in MAC mode.
- `World::addMaterialAtCell(..., Water, ...)`, `replaceMaterialAtCell(..., Water)`, clear, and
  serialization paths know about `waterVolume`.
- `StateGet` includes `WorldData.water_volume`.
- Basic render overlays `waterVolume` onto air cells.
- Streamed Debug render carries an explicit MAC-water overlay.

### Remaining Water Migration Gaps

- Some gameplay and scenario logic still assumes `Cell.material_type == Water`.
- Some code paths still create legacy water cells while MAC mode is active.
- Scenario setup and reset are inconsistent about clearing or repopulating `waterVolume` when a
  `World` instance is reused.

## Guiding Decisions

### 1. Do Not Block Dry Sleeping On Final Water Sleeping

Sleeping state should continue to be computed globally, but `sleep-enforced` behavior should begin as
soon as the dry-region policy is trustworthy.

Initial enforcement should be limited to conservative dry regions. Water regions and dirt/water
boundary regions can remain `tracked-only` until the water-specific quiet model is ready.

### 2. Keep Bulk Water and Droplet Behavior Separate

Large pools should be simulated and eventually slept as bulk MAC water.

If particulate splash or droplet behavior is needed later, it should be implemented as a small
transient spray layer that couples into `waterVolume`, not by making the bulk MAC solver itself
pretend to be droplets.

### 3. Make MAC Water the Single Authoritative Bulk-Water Model

The target end state is:

- bulk water lives in `waterVolume`,
- legacy `Cell.material_type == Water` is not used as an active bulk-water simulation path,
- any remaining water-like transient effects are explicit and local.

### 4. No Serialization Backward Compatibility

- Backward compatibility is explicitly unsupported.
- No migrations, version tags, or compatibility shims should be added for old save formats.

## Delivery Phases

### Phase 1: Enforce Sleeping For Conservative Dry Regions

#### Scope

- Keep computing region sleep state globally.
- Add a distinct "sleep-enforced eligibility" policy instead of treating all tracked sleep state as
  immediately enforceable.
- Initially enforce only for dry, non-organism, non-water-adjacent, not-recently-disturbed regions.
- Use the existing active mask to gate the first world hot loops:
  - force resolution,
  - move generation,
  - closely related per-cell work that must stay consistent with those decisions.

#### Why First

- This converts sleeping from debug/state bookkeeping into real skipped work.
- It validates wake propagation and skipped-work correctness without mixing in unfinished water
  semantics.

#### Acceptance

- Buried dry regions can actually skip force and move work.
- Existing dry-region sleeping tests still pass.
- No obvious wake/regression bugs when a dry sleeping region is disturbed by nearby surface changes.

### Phase 2: Finish MAC Water Ownership And Remove Bulk Legacy Assumptions

#### Scope

- Add a shared helper or query layer for water presence, amount, and edits in MAC mode.
- Migrate remaining scenario/helper logic off direct `Cell.material_type == Water` assumptions where
  the intent is bulk water.
- Audit scenario setup/reset and make `waterVolume` clearing/repopulation explicit when reusing a
  `World`.
- Remove bulk-water creation paths that still instantiate legacy water cells while MAC mode is
  active.

#### Notes

- This does not require designing water sleeping yet.
- The goal is to establish one authoritative bulk-water representation before adding more behavior.
- In practice this phase splits into two kinds of work:
  - straightforward bulk-water migration, such as scenario setup/reset, bulk-water counting,
    hydration, and simple add/remove paths;
  - behavior-coupled legacy-water paths, such as drain behavior, meltdown conversion, and
    collision fragmentation, which need MAC-native replacements rather than mechanical renames.
- Once the migrated helper path exists, old bulk-water APIs and direct legacy-water access patterns
  should be removed rather than left as parallel options.
- The behavior-coupled paths should not be papered over with new compatibility shims. They should
  either become explicit MAC bulk-water operations or move to a separate transient spray/droplet
  system.
- Drain and meltdown behavior in MAC mode should stay conservative during this phase: direct
  bulk-water conversion, query, and removal are in scope, but fake MAC suction or spray heuristics
  are not. Legacy-only force and spray behavior can remain limited to legacy mode until a real
  transient spray/disturbance system exists.

#### Acceptance

- Bulk-water reads/writes go through one consistent abstraction in MAC mode.
- Scenario reset/setup does not leave stale `waterVolume` behind.
- Bulk-water behavior no longer depends on surviving legacy water cells.

### Phase 3: Add Water Quiet Metrics And Sleep Calm Pool Interiors

#### Scope

- Replace the current "water adjacency keeps awake" policy with a more precise interface/disturbance
  policy.
- Derive water quiet metrics from the MAC solver, such as:
  - max face velocity,
  - max local water-volume delta,
  - free-surface presence,
  - recent deposit/removal/displacement,
  - drain/inlet/impact disturbance.
- Allow the interior of large calm pools to qualify for sleep while keeping the active shell awake.

#### Target Behavior

- Pool interiors can sleep.
- Free surfaces, drains, inlets, moving-solid contacts, and recent splash zones stay awake.
- Dirt/water interface regions remain conservative until the policy is clearly stable.

#### Acceptance

- Large calm pools show sleeping interior regions.
- Surface motion or disturbances wake a local halo without requiring full-pool wakeup.
- Water sleeping does not cause obvious mass loss, stuck flow, or missed wake events.

### Phase 4: Gate MAC Water Work By Active Regions If Needed

#### Scope

- If large pools remain expensive after Phase 3, teach `MacProjectionWaterSim` to consume the same
  region activity model.
- Restrict MAC work to active regions plus a required halo, while preserving correct local coupling
  and wake behavior.

#### Why Not Earlier

- This is only worth doing after:
  - sleeping is enforced for dry regions,
  - water ownership is consistent,
  - water quiet metrics exist.
- This should be profiler-driven, not assumed mandatory.

#### Acceptance

- Calm pool interiors avoid unnecessary MAC work.
- Active-shell behavior remains visually and numerically stable.
- Region-boundary artifacts are not introduced by masked MAC updates.

### Phase 5: Add Optional Transient Spray/Droplet Layer

#### Scope

- Introduce a separate transient water-spray system for sparse ballistic droplets.
- Emit droplets only from explicit events:
  - high-energy splash/fragmentation,
  - drain spray,
  - meltdown or similar event-driven effects.
- Re-deposit droplet mass and momentum back into `waterVolume` on impact or settling.

#### Why Last

- This is an optional polish layer.
- It should not be allowed to complicate bulk-water ownership or sleep policy.

#### Acceptance

- Splash effects are local and fun.
- Spray does not force calm pool interiors to remain globally awake.
- Bulk-water sleeping still works with localized spray disturbances.

## Explicit Deferrals

- A perfect unified dirt/water sleep model before any dry sleeping is enforced.
- Full particle-fluid replacement of the MAC solver.
- Surface-tension-heavy droplet hacks inside the bulk MAC volume field.
- Treating transient spray as a second bulk-water simulation.
- Serialization compatibility work of any kind.

## Open Questions

### Water Interface Conservatism

The exact policy for dirt/water boundary regions should remain conservative until MAC-side quiet
metrics are proven stable.

### Droplet Scope

If a transient spray layer is added, it should stay narrowly scoped to sparse splash behavior and
should not become a second bulk-water simulation.
