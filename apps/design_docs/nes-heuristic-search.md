# NES Heuristic Search Option Catalog

Brainstorming doc for using heuristic search as a gameplay and visualization mechanism for NES
games, starting with Super Mario Bros. 1.

This document is intentionally an option inventory, not a recommendation document. The current
goal is to gather plausible design directions and organize them by topic. Choosing between them is
future work.

## Topics

- Current Constraints And Observations
- State Representation And Transpositions
- Action And Segment Representation
- Planner Families
- Objectives, State Evaluation, And Search-Control Heuristics
- Automatic Landmarks And Segment Discovery
- Geometry And Terrain Extraction
- Context-Conditioned Action Libraries
- Neural Network Roles
- Shared Semantics Between Training And Search
- Multi-Game Search Architecture
- Evaluation And Benchmarks
- Visualization Options
- Topic Summary

## Current Constraints And Observations

- SMB1 remains a strong first target because it is highly deterministic and already has useful RAM
  extraction for progress, life state, powerup state, motion, and nearby enemies.
- `absoluteX` remains the dominant progress signal for SMB. Secondary signals may still be useful as
  tie-breakers or mode-specific preferences.
- NES training is already app-headless in the DirtSim sense. A separate option is to build a more
  specialized search runner that captures only the data the search actually needs.
- PPU emulation still dominates a large share of runtime cost, even after major optimization.
- Human labeling effort is intended to be minimal or zero. Automatic landmarks and automatic
  obstacle discovery are therefore more aligned with the current constraints than hand-authored
  level markup.
- The current NN is not yet proven beyond 1-1, which makes it more natural to treat as optional
  support than as a required core component for the first round of design exploration.

## State Representation And Transpositions

### Canonical Search State Options

- `Exact emulator state.` Use a full savestate as the canonical state for correctness and exact
  transposition reuse.
- `RAM-derived state.` Hash a selected subset of SMB RAM fields such as world, level, `absoluteX`,
  screen-space position, velocity, life state, power state, and enemy slots.
- `Hybrid state.` Use exact state for correctness and transpositions, and compute a separate coarse
  feature state for heuristics, bucketing, and pruning.

### Transposition Table Options

- `Pure duplicate suppression.` If the same state has already been seen at equal or lower cost, drop
  the new visit.
- `Best-known arrival cost.` Store the best frame count or path cost that reached each state and
  only revisit if the new path is cheaper.
- `State plus option context.` Treat the same emulator state as distinct if the path history or
  segment context matters for debugging, visualization, or commitment policy.
- `Approximate transpositions.` Merge not only exact matches but also states that appear
  effectively equivalent under a coarse feature representation.

### Dominance And Bucket-Pruning Options

- `No dominance pruning.` Only use exact transpositions and leave approximate pruning for later.
- `Strict dominance.` Within a coarse bucket, drop a state if another state is better on all key
  dimensions.
- `Frontier-first dominance.` Prefer the state that has better frontier progress and lower elapsed
  time, with motion and survival signals as tie-breakers.
- `K-best per bucket.` Keep a small number of diverse high-quality states instead of a single
  winner.

## Action And Segment Representation

### Action Representation Options

- `Fixed hold macros.` Hold a button mask such as `Right+B` for a fixed number of frames.
- `Press/release event sequences.` Search directly over input press and release timings.
- `Parameterized motion primitives.` Use actions such as `run_right(duration)`,
  `run_jump(hold_frames)`, or `turnaround_jump(duration_before_jump, hold_frames)`.
- `Semantic options.` Use higher-level maneuver families such as `clear_gap`, `land_on_pipe`, or
  `flag_finish`.
- `Closed-loop options.` Use options whose stop condition is state-based rather than frame-based,
  such as `hold_right_until_grounded`.

### Segment Representation Options

- `Fixed-length segments.` Plan over a fixed number of future frames.
- `Variable-length segments.` Let segment duration depend on context or candidate policy.
- `Condition-terminated segments.` End segments on conditions such as grounded landing, death,
  transition, or frontier gain.
- `Landmark-terminated segments.` Plan from one anchor to the next rather than over a fixed time
  window.
- `Two-stage segments.` Use a coarse segment first and refine the timing near the end.

### Coarse-To-Fine Options

- `Coarse action search plus fine timing refinement.` Search over maneuver families first, then
  refine press and release timing.
- `Short-horizon event search near hazards.` Use a richer action language only in contexts that
  actually need it.
- `Option library plus segment optimizer.` Search over a compact action library and let a local
  optimizer tune its parameters.

## Planner Families

### State-Space Planner Options

- `Best-first graph search.` Expand the most promising state next using a heuristic and exact
  transpositions.
- `A* or weighted A*.` Treat SMB as a cost-plus-heuristic graph search problem, with weighted A*
  favored if speed matters more than optimality guarantees.
- `Beam search.` Keep only the top-N states at each depth.
- `MCTS.` Explore a search tree adaptively and allocate effort toward promising branches.

### Sequence-Optimization Options

- `Rolling Horizon Evolutionary Algorithm.` Evolve short input sequences from the current root
  state.
- `Cross-Entropy Method (CEM).` Sample many candidate sequences, keep the elites, and bias the next
  sampling round toward them.
- `Model Predictive Path Integral (MPPI).` Start from a current best segment plan, perturb it many
  times, and shift it toward the higher-scoring perturbations.
- `Random or elite shooting.` Sample many sequences and keep only the best, with little or no model
  update between rounds.

### Hierarchical And Hybrid Planner Options

- `Landmark graph search plus local optimizer.` Search over anchors globally and optimize each local
  segment separately.
- `Beam search plus timing refinement.` Use beam search to identify promising maneuver families and
  refine timing only for survivors.
- `MCTS plus policy prior.` Use a policy model to bias action ordering or rollout behavior.
- `NN baseline plus planner challenger.` Let the NN and planner attempt the same segment and choose
  the better result.

## Objectives, State Evaluation, And Search-Control Heuristics

### Top-Level Objective Options

- `Pure frontier objective.` Maximize best `absoluteX` reached.
- `Frontier plus time.` Prefer reaching the same frontier sooner in game frames.
- `Landmark completion objective.` Treat reaching the next anchor safely as the main goal.
- `Robustness objective.` Prefer plans with larger success margins or more stable post-segment
  states.
- `Mode-specific objective.` Keep frontier dominant but vary tie-breakers for speedrun, safety, or
  powerup-preserving modes.

### State-Evaluation Options

- `Best-frontier evaluation.` Score states using best frontier reached so far, not only current
  position.
- `Velocity-aware evaluation.` Use speed and motion quality as tie-breakers for future promise.
- `Landing-quality evaluation.` Prefer stable grounded states over awkward airborne states at
  similar progress.
- `Recovery-potential evaluation.` Distinguish controlled survival from states that are alive but
  nearly doomed.
- `Hazard-margin evaluation.` Prefer plans that clear pits, enemies, or stairs with more margin.
- `Phase-specific evaluation.` Use different tie-breakers depending on flat ground, jump arcs,
  hazard approach, or level transitions.
- `Learned value estimate.` Later option: use a learned evaluator to estimate future promise.

### Search-Control Heuristic Options

- `Action ordering.` Expand the most sensible actions first.
- `Branch suppression.` Limit branching on safe flat ground.
- `Hazard-triggered branching.` Increase branching near pits, enemy clusters, transitions, and
  hotspots.
- `Adaptive horizon.` Use short lookahead in easy regions and deeper lookahead in timing-critical
  regions.
- `Dominance pruning.` Use coarse feature dominance to shrink the search space.
- `K-best per bucket.` Preserve some diversity instead of collapsing early to one local style.
- `Novelty or diversity bonus.` Intentionally keep some structurally different candidates alive.
- `Optimistic bounds.` Use a rough upper bound on future progress to stop exploring hopeless
  states.

## Automatic Landmarks And Segment Discovery

### Landmark Options

- `Progress landmarks.` Use fixed `absoluteX` buckets, screen buckets, or half-screen progress
  intervals.
- `Hard RAM-transition landmarks.` Trigger anchors on pipe entry, vine climb, level transition,
  powerup transition, and similar explicit state changes.
- `Safe-anchor landmarks.` Detect grounded, stable, post-hazard states that are good segment roots.
- `Failure-cluster landmarks.` Discover hotspots by clustering deaths, stalls, or severe slowdowns
  from many baseline runs.
- `Trajectory landmarks.` Reuse automatically detected jump launches, landings, recoveries, and
  other motion events as anchor candidates.
- `Geometry-derived landmarks.` Infer anchors from terrain or level structure once geometry
  extraction exists.

### Failure-Cluster Options

- `Death clusters.` Cluster repeated deaths by frontier position.
- `Stall clusters.` Cluster repeated no-progress or low-progress regions.
- `Velocity-collapse clusters.` Detect locations where policies repeatedly lose momentum.
- `Transition-failure clusters.` Detect recurring misses of pipe entry, staircase timing, and other
  special segments.
- `Mixed-policy hotspot map.` Combine failures from the NN, simple scripted baselines, and shallow
  planners to find general difficulty regions rather than only model-specific weaknesses.

### Motion-Context Landmark Options

- `Jump launch landmarks.` Detect grounded-to-airborne transitions caused by deliberate jump input.
- `Landing landmarks.` Detect stable airborne-to-grounded transitions after successful jumps.
- `Fall landmarks.` Distinguish airborne states caused by falling off support from intentional jump
  arcs.
- `Recovery landmarks.` Detect when Mario regains stable control after an unstable phase.

The jump-versus-fall distinction is especially valuable because it adds meaningful context without
manual level labeling.

## Geometry And Terrain Extraction

### Extraction Source Options

- `PPU or nametable extraction.` Expose internal PPU background state and derive terrain from it.
- `SMB-specific RAM extraction.` Reverse-engineer SMB's own level or object data structures and
  decode terrain from game memory.
- `Palette or video extraction.` Infer terrain from palette-index or RGB frames.
- `Hybrid extraction.` Combine geometry from PPU or RAM with actor state from existing SMB RAM
  extraction and hotspot information from observed failures.

### Derived Geometry Products

- `Support map.` Estimate where stable ground exists ahead of Mario.
- `Gap and ledge map.` Detect likely pits, drops, and unsafe empty spans.
- `Obstacle-class tags.` Label segments as likely pit, enemy cluster, pipe, stairs, flag finish,
  or tunnel-like constraints.
- `Safe-ground candidates.` Detect likely anchor regions without full semantic labeling.

### Scope-Control Options

- `Failure-guided geometry mining.` Only extract and classify terrain around already discovered
  hotspots.
- `Coarse structure only.` Start with walkable versus non-walkable support and avoid rich
  semantics.
- `Full semantic extraction later.` Leave richer object and tile understanding as a later layer.

## Context-Conditioned Action Libraries

### Conditioning Signal Options

- `Flat versus hazard-nearby.` Split between easy and hard contexts.
- `Grounded versus airborne.` Use different option families depending on support state.
- `Jumped versus fell.` Distinguish intentional airborne motion from failure airborne motion.
- `Enemy-pressure aware.` Use nearby enemy state to widen or narrow the action set.
- `Transition-state aware.` Use pipe, vine, powerup, and level-transition phases as special
  contexts.
- `Geometry-aware.` Later option: use terrain-derived segment classes to choose action families.

### Context-Specific Option Families

- `Safe flat ground library.` Mostly preserve speed and move right.
- `Hazard-approach library.` Expand jump timing and maneuver variants.
- `Intentional jump library.` Search over jump-hold duration and steering while airborne.
- `Fall recovery library.` Use a smaller set focused on salvage rather than normal traversal.
- `Enemy-cluster library.` Include enemy-specific hops, timing variants, and speed control.
- `Transition library.` Use special options for pipes, vines, stairs, and flag sequences.

### Structural Options

- `Single universal action library.` Same option set everywhere.
- `Context-conditioned library.` Switch option families based on runtime context.
- `Hierarchical option grammar.` Let context determine which maneuver families are even legal to
  consider next.

## Neural Network Roles

At the current stage, the NN fits more naturally as optional support than as a required planning
component.

### Near-Term NN Roles

- `Baseline comparator.` Let the NN attempt the same segment and compare it directly with planner
  output.
- `Easy-stretch autopilot.` Use the NN only on already-solved or low-risk regions.
- `Candidate generator.` Let the NN propose candidate segment plans for search to refine or defeat.

### Later NN Roles

- `Action-ordering prior.` Use the NN to rank candidate options before search expands them.
- `Rollout policy.` Use the NN inside MCTS or other tree search instead of random rollouts.
- `Value estimator.` Use the NN to estimate state promise without directly controlling the game.
- `Training target consumer.` Distill successful search results into a faster reactive NN policy.

## Shared Semantics Between Training And Search

The current NES design already suggests a reusable pattern:

- extract game-specific RAM state
- convert it into a typed per-game state
- evaluate that state using per-game logic

For heuristic search, an important design question is how much of that should be shared directly
with training.

### Shared-Layer Options

- `Shared extractor only.` Training and search both use the same per-game RAM extractor, but have
  separate evaluation and planning logic.
- `Shared extractor and evaluator.` Training and search both use the same typed state extraction and
  the same evaluation logic.
- `Shared semantics package.` Training and search share a per-game package containing extraction,
  terminal detection, progress logic, event detection, and evaluation updates.
- `Separate training and search semantics.` Keep the current training-oriented adapter path and build
  a distinct search-oriented semantics layer later.

### Reusable Shared Components

- `MemorySnapshot -> typed game state.`
- `typed game state -> evaluation or progress update.`
- `terminal detection.`
- `motion-context event detection.` Examples include jump launch, fall, landing, and recovery.
- `setup-state detection.` Examples include title, waiting, gameplay, transition, and failure
  phases.

### Evaluator-State Options

If search wants to reuse the exact same evaluation semantics as training, search nodes may need to
carry evaluator state in addition to emulator state.

- `Emulator state only.` The evaluator is recomputed or simplified from scratch at each state.
- `Emulator state plus evaluator state.` Each search node carries the extra evaluator bookkeeping
  needed for exact agreement with training semantics.
- `Derived evaluator summary.` Carry a reduced evaluator state such as best frontier, frames since
  progress, and accumulated reward totals instead of a full evaluator object.

### Setup-Script And Bootstrapping Options

Setup scripts are useful not only for training but also for search bootstrapping.

- `Shared setup script.` Use the same scripted setup path to reach a canonical gameplay start state
  before either training or search begins.
- `Search-specific root setup.` Allow search to start from a different scripted or saved root than
  training if a segment-specific start state is needed.
- `Power-on canonical root.` Always reproduce the same full boot path from power-on.
- `Saved-root canonical root.` Store specific search roots after setup and start search from those
  directly.

### Search-Specific Additions On Top Of Shared Semantics

- `action library.`
- `landmark detection and segment policy.`
- `transposition keys.`
- `dominance and pruning rules.`
- `search-specific heuristics and budget allocation.`

## Multi-Game Search Architecture

The current per-game RAM extractor pattern invites a broader question: what would it mean for a
heuristic searcher to work across many supported NES games?

### Genericity Options

- `Framework-general.` Use one shared planner core, but require a per-game search adapter.
- `Extractor-general.` Treat the existence of a RAM extractor as the main prerequisite for search.
- `Semantics-general.` Require per-game extraction plus progress, terminal, and action semantics.
- `Fully generic.` Try to make search work on any supported ROM with minimal game-specific logic.

### Per-Game Search Adapter Options

A future search adapter could be lighter and more planner-oriented than the current training
adapter.

- `Reuse current NesGameAdapter directly.` Extend the existing adapter interface to serve both
  training and search.
- `Introduce a search-specific adapter.` Create a new per-game search interface focused on state
  extraction, objective semantics, landmarks, and action libraries.
- `Split semantics from orchestration.` Factor extraction and evaluation into reusable per-game
  semantics while keeping training-control and search-control layers separate.

### Minimal Per-Game Search Contract Options

- `typed state extraction.`
- `terminal detection.`
- `progress or objective update.`
- `optional landmark and event detection.`
- `optional action library or option grammar.`
- `optional exact and coarse state keys for transpositions and pruning.`

### Support-Tier Options

- `Tier 1: Searchable.` The game provides typed extraction, terminal detection, a progress
  objective, and a basic action library.
- `Tier 2: Segment-aware.` The game also provides landmarks, motion-context events, and richer
  heuristics.
- `Tier 3: Rich visualization.` The game also provides geometry extraction, hotspot overlays, and
  segment classification.

## Evaluation And Benchmarks

### Primary Outcome Metrics

- `Next-landmark solve rate.` Measure how often a planner reaches the next anchor from a fixed root
  state.
- `Wall-clock time to first success.` Important for interactive use and watchability.
- `Wall-clock time to best-known segment.` Useful for anytime planners.
- `Frontier gained per second.` Useful when no segment success is found.
- `End-to-end level progress.` Coarse but still useful later.

### Search-Efficiency Metrics

- `Nodes expanded or sequences evaluated.`
- `Unique exact states reached.`
- `Transposition hit rate.`
- `Dominance-prune rate.`
- `Effective search depth reached.`
- `Budget spent in hotspot regions versus easy regions.`

### Quality Metrics

- `Plan robustness.` Sensitivity to small timing perturbations or slight root-state variation.
- `Landing quality or stability.` Whether the end state is easy to continue from.
- `Margin of success.` Barely clearing an obstacle versus clearing it comfortably.
- `Replay cleanliness or watchability.` Subjective but still relevant for the visualization goal.

### Benchmark Granularity Options

- `Micro-benchmarks.` Single hotspot or single segment from a fixed root state.
- `Meso-benchmarks.` Small chains of consecutive segments.
- `Macro-benchmarks.` Full-level or multi-level play.

### Comparison Set Options

- `Simple scripted baseline.`
- `NN-only baseline.`
- `Beam-search baseline.`
- `Best-first graph-search baseline.`
- `CEM baseline.`
- `RHEA baseline.`
- `MCTS baseline.`

## Visualization Options

One design axis for visualization is whether it emphasizes truthful search artifacts or more
decorative presentation layers that may obscure what the algorithm is actually doing.

### Low-Cost Visualization Options

- `Level-progress strip.` Show current frontier, hotspots, deaths, stalls, anchors, and solved
  regions along a horizontal progress axis.
- `Failure histogram.` Show density of repeated failures or stalls by frontier position.
- `Segment status panel.` Show current root, current target anchor, current best candidate, and
  elapsed search time.
- `Trajectory overlay on simplified map.` Draw attempted and successful paths over a lightweight
  representation of the level.

### Comparison Visualization Options

- `Planner versus NN on the same root and target.`
- `Planner versus planner on the same segment.`
- `Baseline versus refined search on the same hotspot.`

### Developer-Facing Diagnostic Options

- `Transposition hit visualization.`
- `Dominance-prune counters and trends.`
- `Bucket occupancy and diversity loss.`
- `Search-budget allocation across hotspots and easy regions.`

### Richer Later Visualization Options

- `Ghost trajectories over a frozen game frame.`
- `Multi-viewport candidate comparisons.`
- `Rewind-and-compare playback of competing segment solutions.`

## Topic Summary

The major topic buckets gathered so far are:

- state representation, transpositions, and dominance pruning
- action representation and segment structure
- planner families, including graph search, beam search, MCTS, RHEA, CEM, and MPPI
- objectives, evaluation functions, and search-control heuristics
- automatic landmarks, especially failure-cluster and motion-context landmarks
- geometry and terrain extraction with minimal manual labeling
- context-conditioned action libraries
- optional NN roles now and later
- evaluation and benchmark design
- truthful and low-cost visualization options

Future work can narrow these options into a concrete plan.
