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

1. Lock the Phase 3 architecture: water wake policy stays global to the world / MAC solver /
   region tracker rather than being implemented in scenario code.
2. Run `ScenarioRunner::tick()` at the start of the frame so authored water inputs affect that
   frame's MAC solve and tracker evaluation.
3. Finish the tracked-only Phase 3 water quiet-state slice and validate calm pool interiors.
4. Tune the first water wake policy for interfaces, drains, inlets, and impacts using centralized
   solver/world signals.
5. Gate MAC water work by active regions if profiling shows it is still needed.
6. Add an optional transient spray/droplet layer.

## Current Branch Status

### Sleeping

- `WorldRegionActivityTracker` is wired into the frame loop and computes region state, wake reasons,
  and active masks.
- External edits, blocked transfers, gravity changes, and material moves already feed the tracker.
- Region debug overlay plumbing exists and is visible in the UI.
- Selected dry-region hot loops now enforce sleeping for conservative dry interiors.
- Calm MAC pool interiors can already reach `Sleeping` tracker state in the first tracked-only
  water slice.
- Water regions and dirt/water boundary regions remain `tracked-only`; water-region work is not yet
  skipped.
- Static-load work, MAC-water work, and some dry-region processing are still not gated by the sleep
  mask.

### Water

- `WaterSimMode::MacProjection` is the global default.
- Separate-layer `waterVolume` is the canonical bulk-water state in MAC mode.
- Explicit bulk-water APIs, clear, and serialization paths know about `waterVolume`.
- `StateGet` includes `WorldData.water_volume`.
- Basic render overlays `waterVolume` onto air cells.
- Streamed Debug render carries an explicit MAC-water overlay.
- Production scenario water sources now author bulk water through explicit bulk-water APIs instead
  of instantiating legacy water cells in MAC mode.
- Generic material mutation no longer authors bulk water in MAC mode.
- Editor/server/CLI water commands now use explicit `BulkWaterSet` / `SpawnWaterBall` paths
  instead of overloading solid-material commands.
- `CellSet` is back to meaning air/solid cell edits only; it no longer carries a hidden water
  payload path.
- `SpawnDirtBall` is literal dirt again; water spawning is an explicit peer command rather than a
  selected-material special case.
- Top-level scenario setup/reset paths now clear `waterVolume` explicitly when a `World` is reused.

### Phase 2 Completion Notes

- No known active MAC bulk-water authoring path now depends on `Cell.material_type == Water`.
- Remaining `Material::EnumType::Water` references are expected legacy-mode physics, rendering,
  palette/sensing, or explicit helper/cleanup code around bulk-water APIs.
- If a new gameplay path needs bulk-water authoring, it should use the explicit bulk-water APIs
  rather than reopening generic material mutation.

### Phase 2 Status

Phase 2 is complete on this branch.

The following are now true:

- production MAC bulk-water behavior no longer depends on legacy water cells,
- reused worlds do not carry stale `waterVolume`,
- no active public/editor command path overloads solid-material APIs to mean bulk water,
- any remaining legacy-water code is either truly legacy-mode-only or explicitly out of the bulk
  MAC ownership path.

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

### 4. Keep Water Wake Policy Global

Water wake/sleep policy belongs in shared simulation systems:

- `World`,
- `MacProjectionWaterSim`,
- `WorldRegionActivityTracker`.

Scenario code may author physical inputs such as rain, drains, or explicit bulk-water edits, but it
should not decide wake halos, paint tracker state, or add scenario-specific heuristics for bulk
water sleeping.

If a water disturbance signal is needed for sleeping, it should come from one centralized world- or
solver-level path rather than ad hoc calls from individual scenarios.

### 5. Water Inputs Must Enter Before Sleep Evaluation

Scenario-authored water edits should happen during the same start-of-frame authoring phase as other
scenario updates.

What matters is that a scenario-authored water edit is applied before the MAC water step and before
that frame's region activity summary.

That means:

- `ScenarioRunner::tick()` should run at the start of the frame on the committed world state from
  the end of the previous frame,
- water-affecting authored inputs should be applied immediately during that start-of-frame tick,
- the first rendered frame that shows an authored water edit should also be the first frame whose
  MAC solve and sleep tracker have seen it.

The sleep model should not depend on late scenario code manually compensating for frame-ordering
problems.

### 6. No Serialization Backward Compatibility

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

#### Status

- Complete on the current branch.
- Reopen only if a new active MAC bulk-water ownership leak is found.

#### Scope

- Add a shared helper or query layer for water presence, amount, and edits in MAC mode.
- Migrate remaining scenario/helper logic off direct `Cell.material_type == Water` assumptions where
  the intent is bulk water.
- Audit scenario setup/reset and make `waterVolume` clearing/repopulation explicit when reusing a
  `World`.
- Remove bulk-water creation paths that still instantiate legacy water cells while MAC mode is
  active.
- Keep the command/editor surface explicit:
  - cell material edits stay separate from bulk-water edits,
  - dirt spawn and water spawn stay separate,
  - no new mixed "selected material" or `CellSet(WATER)` style paths are added.

#### Notes

- This does not require designing water sleeping yet.
- The goal is to establish one authoritative bulk-water representation before adding more behavior.
- The command-surface and scenario-reset cleanup is complete on the current branch.
- In practice this phase splits into two kinds of work:
  - straightforward bulk-water migration, such as scenario setup/reset, bulk-water counting,
    hydration, and simple add/remove paths;
  - behavior-coupled legacy-water paths, such as drain behavior, meltdown conversion, and
    collision fragmentation, which need MAC-native replacements rather than mechanical renames.
- Once the migrated helper path exists, old bulk-water APIs and direct legacy-water access patterns
  should be removed rather than left as parallel options.
- Legacy-mode-only tests or scaffolding do not define the MAC ownership boundary. They can remain
  temporarily if they are clearly outside the active MAC bulk-water path.
- The behavior-coupled paths should not be papered over with new compatibility shims. They should
  either become explicit MAC bulk-water operations or move to a separate transient spray/droplet
  system.
- Drain and meltdown behavior in MAC mode should stay conservative during this phase: direct
  bulk-water conversion, query, and removal are in scope, but fake MAC suction or spray heuristics
  are not. Legacy-only force and spray behavior can remain limited to legacy mode until a real
  transient spray/disturbance system exists.
- If a scenario still needs authored drain guidance in MAC mode, prefer a reusable guided-drain
  primitive with an explicit guide band and mouth band over a world-scale attractor or ad hoc
  cell-water hacks.

#### Acceptance

- Bulk-water reads/writes go through one consistent abstraction in MAC mode.
- Scenario reset/setup does not leave stale `waterVolume` behind.
- Bulk-water behavior no longer depends on surviving legacy water cells.
- Production MAC bulk-water behavior no longer depends on generic water-material mutation paths.
- Public/editor command surfaces do not overload solid-material commands to author bulk water.
- The items above are satisfied on the current branch.

### Phase 3: Add Water Quiet Metrics And Sleep Calm Pool Interiors

#### Scope

- First, fix the frame plumbing so `ScenarioRunner::tick()` runs at the start of the frame.
- Scenario-authored water inputs should then be applied before the MAC solve and before region
  sleep state is evaluated for that frame.
- Keep a follow-up note to evaluate moving `OrganismManager::update()` into the same early authoring
  phase so organism-authored cell changes also participate in the same frame's solver/tracker
  inputs. This is intentionally out of scope for the current sleep/water slice unless a concrete
  bug requires it.
- Replace the current "water adjacency keeps awake" policy with a more precise interface/disturbance
  policy.
- Derive water quiet metrics from the MAC solver and centralized world-level water-input signals,
  such as:
  - max face velocity,
  - max local water-volume delta,
  - interface-only face-speed disturbance,
  - recent centralized deposit/removal/displacement,
  - drain/inlet/impact disturbance.
- Allow the interior of large calm pools, and calm portions of the exposed shell, to qualify for
  sleep while disturbed surface regions stay awake.
- Start with a tracked-only slice:
  - compute and expose the new MAC-water quiet state in the region tracker and debug overlays,
  - use shared solver/world signals rather than scenario-specific wake code,
  - keep solver face-speed metrics available for diagnostics and tuning, and prefer them over
    scenario-authored wake heuristics when choosing the first automatic disturbance policy,
  - do not skip MAC solver work or water-region world work yet.

#### Guardrails

- Do not add scenario-specific tracker pokes, wake rectangles, or wake halos for bulk water.
- Do not let scenario integration tests define the wake policy; they should only validate that
  scenario-authored water inputs route into the same global water/sleep systems as every other
  source.
- If authored rain, drain, or removal operations need an explicit disturbance signal, expose it
  through a centralized world/water path rather than direct scenario-to-tracker wiring.

#### Target Behavior

- Pool interiors can sleep.
- Calm free surfaces can quiet when their disturbance signals stay below threshold.
- Disturbed free surfaces, drains, inlets, moving-solid contacts, and recent splash zones stay
  awake.
- Dirt/water interface regions remain conservative until the policy is clearly stable.
- The first implementation slice changes tracker state only; runtime enforcement stays off for water.
- Scenario code remains responsible for authoring physical water inputs, not for choosing wake
  policy.

#### Acceptance

- Large calm pools show sleeping interior regions.
- Surface motion or disturbances wake a local halo without requiring full-pool wakeup.
- Water sleeping does not cause obvious mass loss, stuck flow, or missed wake events.
- During the first tracked-only slice, water regions can become `Sleeping` in debug/tracker state
  without yet skipping MAC work.
- No scenario-specific wake heuristics are required to make the tracked-only slice behave
  correctly.

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
