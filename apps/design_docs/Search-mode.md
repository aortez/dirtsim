# Search Mode

## Status

Draft implementation planning document.

This document describes how to add a first-class search workflow to DirtSim without overloading the existing training workflow. 

It covers UI states, server states, data models, APIs, persistence, and search design and heuristics.

For search and heuristic options, see `nes-heuristic-search.md` - this is a bag of options, we'll discuss before adding any of these details to the main document here.

## Overview

DirtSim currently has two major interactive workflows:

- run a scenario live
- run an evolution training session

Heuristic Search does not fit cleanly into either existing workflow:

- it is not a new scenario identity
- it is not evolution training
- it does need a long-lived compute session, progress reporting, playback, and saved results

The recommended direction is:

- keep `Scenario` as world identity
- keep `Training` as genome/evolution workflow
- add a separate top-level `Search` workflow as a sibling to `Training`
- reuse shared execution and UI shell pieces where practical

This Search workflow will support NES at first.
Then later, it will support other gridworld scenarios, such as Duck in Clock, or Tree in other scenarios.
We don't need to design for any flexiblity beyond NES SMB1 at first though.

## Problem Statement

We need an implementation path for Search that:

- fits the current state-machine architecture
- can generalize beyond NES scenarios
- can reuse enough of the training/live-render shell to avoid duplicate infrastructure

## Goals

- Add a first-class top-level Search mode.
- Model Search as explicit UI and server states.
- Keep scenario selection general and reusable across NES and non-NES Search targets.
- Separate Search session persistence from genome/training-result persistence.
- Reuse shared scenario semantics between training and Search where that makes sense.
- Support live progress, pause/stop, and best playback, during Search runs.
- Allow phased delivery.

## Non-Goals

- Making every scenario Search-capable in the first phase.

## Terms

- `Scenario`: the world identity and config that the server can run, such as `Clock` or `NesSuperMarioBros`.
- `Training`: a learning workflow that evaluates populations and produces genomes.
- `Search`: a search workflow that evaluates candidate trajectories or states and produces one or more plans, traces, or ranked outcomes.
- `PlayerControlFrame`: one fixed-timestep gameplay-control state for a player. This is a quantized virtual controller layer, not raw hardware gamepad state.
- `Plan`: a fixed-timestep sequence of `PlayerControlFrame` values that Search returns as its current best answer.
- `Player`: the controllable subject within a scenario for Search purposes, such as `player1` in SMB or `duck` in Clock.


## Summary

- Add `Search` as a top-level workflow, not as a sub-tab inside `Training`.
- Keep `Scenario` as world identity only.
- Introduce new UI states:
  - `SearchIdle`
  - `SearchActive`
  - `PlanPlayback`
- Introduce new server states:
  - `SearchActive`
  - `PlanPlayback`
- Add Search-specific results: Plans.
- Reuse the same scenario initialization path that training already uses.
- Keep shared scenario semantics below both workflows, but keep training orchestration and search orchestration separate.

## Existing stuff

The current architecture already has some useful related stuff:

- `StartMenu` treats simulation and training as separate transitions.
- `SimRunning` already assumes that the scenario panel is for world-specific controls.
- `TrainingIdle` and `TrainingActive` already provide a dedicated long-running compute workflow.
- `TrainingActiveView` already has reusable live playback concepts:
  - render stream configuration
  - best playback
  - scenario-controls flyout
  - pause and stop
- `TrainingResult` and the genome browser are tightly coupled to evolution output and should stay that way.

We'll add a separate Search state with partial reuse of the Training active shell.

## Relevant UI State Machine

```text
StartMenu
  -> SearchIdle

SearchIdle
  -> SearchActive
  -> PlanPlayback
  -> StartMenu

SearchActive
  -> SearchIdle

PlanPlayback
  -> SearchIdle
```

## UI States

### Start Menu

Add a top-level `Search` entry from `StartMenu`, parallel to `Scenario` and `Evolution`.
Do we have a map icon we can use?  needs planning.

### SearchIdle (new UI state)

`SearchIdle` is the Search launcher and Plan browser.

- Edit Search config and start Searches.
- Browse prior Search results (Plans); select for playback.
- Own the current Plans list and selected Plan.

Primary panels:

- `Search Launcher` Configure and start search.
Any options are persisted in a new Search-specific section in the user settings.
later: choose scenario and for non-nes scenarios, choose organism type.
- `Plans`  browse plan summaries and launch replays

### Search Active (new UI state)

`SearchActive` is the live search state.

- title and status
- show search progress and status
- render best playback or best snapshot
- show search metrics and diagnostics
- owns pause/stop and searchs stream settings
- allow pause and stop

The active search screen should visually resemble the current training active shell, but its language and metrics must be search-specific rather than evolution-specific.

#### Completion Behavior

- auto-store search session results
- return directly to `SearchIdle` after completion or stop
`SearchActive`, with `Plans` open and last saved plan selected.

### PlanPlayback (new UI state)

`PlanPlayback` only plays back a plan.

- title and status (time, current evaluation (rolling), final score)
- pause/stop
- rendering of playback

#### Completion Behavior

- return directly to `SearchIdle` when playback stops or reaches the end
- restore `SearchIdle`, with `Plans` open and the last played plan selected

#### Transition Rules

- `SearchIdle` is the only entry point for starting a new search
- `SearchIdle -> SearchActive` when the user starts a new Search
- `SearchIdle -> PlanPlayback` when the user opens a saved Plan
- `SearchActive -> SearchIdle` on completion or stop
- `PlanPlayback -> SearchIdle` on stop, back, or playback completion
- Pause is runtime status within `SearchActive` and `PlanPlayback`, not a separate UI state
- Phase 1 does not support direct `PlanPlayback -> SearchActive`; return to `SearchIdle` first

## Relvant Server State Machine

```text
Idle
...
  -> SearchActive
  -> PlanPlayback

SearchActive
  -> Idle

PlanPlayback
  -> Idle
```

### Server States

`SearchActive`

- owns the active search session
- owns search runner execution
- owns progress broadcast cadence
- owns best playback or best snapshot emission
- owns result persistence on completion or stop
- allows for pausing/resume of search

`PlanPlayback`

- plays back a saved plan using the same scenario initialization path as training
- owns playback timing, pause state, and playback frame emission
- returns to `Idle` when stopped or when playback completes

## Execution Model

- `search runner` evaluates the active search policy and produces progress and a candidate plan
- `playback runner` replays the current best candidate for the UI

This mirrors an existing training pattern where the active UI shows a live best playback rather than every raw evaluation.

Phase 1 implementation:

- one search session
- one best-playback runner
- one stream of search progress
- `SearchStart` is empty for Phase 1
- Search uses the same scenario initialization path and SMB evaluator path as training
- the Phase 1 search policy is fixed: emit `PlayerControlFrame{ .xAxis = 127 }` for every frame
- each advanced gameplay frame appends one frame to the candidate `Plan`
- best playback is enabled by default
- stop conditions match SMB training: end on life loss, or after 1800 gameplay frames without frontier improvement
- on completion, save one `Plan` and return to `SearchIdle`

## Shared Infrastructure With Training

Search should not reuse the current training states directly, but it should reuse infrastructure below the state boundary where practical. 

Extract reusable pieces when search would otherwise copy existing training code wholesale.

Candidate shared pieces:

- render stream subscription plumbing
- stream interval settings
- best playback plumbing
- pause/stop command patterns
- browser-panel patterns for results/history
- common active-screen layout primitives

## Shared Semantics Layer

Search and training should share scenario semantics where that is valuable, but they should not share orchestration or result models.

Re-use NES startup sequence from training.  Each ROM has a startup sequence, this can be used by both Search and training.

Re-use NES evaluation from training.  The evaluation state can follow the search state as necessary.

## Generalizing Beyond NES

Search must not assume an NES-specific model.

Non-NES scenarios will have an associated `OrganismType`, such as duck, tree, or goose.

## Search Capability Discovery

API:
- `SearchScenariosGet` returns vector<Scenario::EnumType> scenarios;

## Proposed Data Model

### Search Progress

```cpp
struct SearchProgress {
    bool paused = false;
    double elapsedSeconds = 0.0;
    uint64_t elapsedFrames = 0;
    double bestFrontier = 0.0;
};
```

- Phase 1 progress only reports truthful rollout metrics.
- Search-specific diagnostics such as node counts are deferred until later phases.

### Plans

```cpp
using PlanId = std::string; // UUID

namespace PlayerControlLayout {
inline constexpr uint8_t ButtonA = 1u << 0;
inline constexpr uint8_t ButtonB = 1u << 1;
inline constexpr uint8_t ButtonX = 1u << 2;
inline constexpr uint8_t ButtonY = 1u << 3;
inline constexpr uint8_t ButtonSelect = 1u << 4;
inline constexpr uint8_t ButtonStart = 1u << 5;
}

struct PlayerControlFrame {
    int8_t xAxis = 0; // -127..127
    int8_t yAxis = 0; // -127..127
    uint8_t buttons = 0;
};

struct PlanSummary {
    PlanId id;
    uint64_t elapsedFrames = 0;
    double bestFrontier = 0.0;
};

struct Plan {
    PlanSummary summary;
    std::vector<PlayerControlFrame> frames;
};
```

- `Plan` stores gameplay controls for each fixed timestep.
- This is a shared virtual-controller layer that can be adapted to NES and grid-based players.
- NES-style direction input is derived from the signs of `xAxis` and `yAxis`.
- Phase 1 plan metadata is intentionally minimal: `id`, `elapsedFrames`, and `bestFrontier`.
- Exact `Plan` attachment to scenario and player/organism identity is deferred until Phase 3.

## Proposed API Surface

### UI -> Server Commands

- `SearchCatalogGet`
- `SearchStart`
- `SearchPauseSet`
- `SearchStop`
- `PlanList`
- `PlanGet`
- `PlanDelete`
- `PlanPlaybackStart`
- `PlanPlaybackStop`
- `PlanPlaybackPauseSet`

Rule:
- `PlanList`, `PlanGet`, and `PlanDelete` are only valid from `SearchIdle` on the UI and `Idle` on the server.

Phase 1 shapes:

```cpp
struct SearchStart {};

struct PlanGet {
    PlanId id;
};

struct PlanDelete {
    PlanId id;
};

struct PlanPlaybackStart {
    PlanId id;
};
```

### Server -> UI Broadcasts

- `SearchProgress`
- `SearchBestSnapshot`
- `SearchBestPlaybackFrame`
- `PlanSaved` (completion)
- `PlanPlaybackStopped`

Phase 1 shapes:

```cpp
using PlanSaved = PlanSummary;

enum class PlanPlaybackStopReason {
    Stopped,
    Completed,
};

struct PlanPlaybackStopped {
    PlanId id;
    PlanPlaybackStopReason reason;
};
```

### Plan Repository

Plans need their own repository: `PlanRepository`

Phase 1 repository shape:

- `PlanList` returns `std::vector<PlanSummary>`
- `PlanGet` returns `Plan`
- `PlanDelete` deletes by `PlanId`
- `PlanSaved` broadcasts `PlanSummary`
- `PlanPlaybackStopped` broadcasts the played `PlanId` and whether playback was stopped or completed

## Phased Rollout

### Phase 1: Search shell

- add `SearchIdle`, `SearchActive`, `PlanPlayback`
- add Search start/stop/pause/progress APIs
- add dedicated Plan types and repository
- minimal Search config panel and active screen
- support `NesSuperMarioBros` only
- brain dead search - hold right
- add basic functional tests for search and playback

Success criteria:

- Search is reachable from `StartMenu`
- Search can run, stream progress, and stop cleanly
- Plans are persisted and browsable
- brain dead search - run right until death by first goomba
- Playback can play back saved Plan and return cleanly to idle on completion or stop
- a functional test can run hold-right search and verify a saved `Plan`
- a functional test can allow playback for a saved `Plan` to complete and return cleanly to idle
- a functional test can stop playback for a saved `Plan` and return cleanly to idle

Phase 1 functional tests:

- `canSearchHoldRight`
  restart services, start Search, wait for completion, verify `PlanList` grows by one, then `PlanGet` the new `Plan` and verify `elapsedFrames > 0`, `bestFrontier > 0`, and all frames match the hold-right policy
- `canPlaybackPlan`
  create or load a saved `Plan`, start playback, verify UI and server enter `PlanPlayback`, allow playback to complete naturally, and verify both return cleanly to idle
- `canStopPlaybackPlan`
  create or load a saved `Plan`, start playback, verify UI and server enter `PlanPlayback`, stop playback, and verify both return cleanly to idle
- `canPauseSearch`
  start Search, pause it, verify `SearchProgress.paused == true`, verify `elapsedFrames` stops advancing while paused, then resume and verify progress continues

### Phase 2: Basic Search implementation.

Root and checkpoint model:
  - Search starts from the standard SMB gameplay root produced by the shared setup path.
  - During a search, successful checkpoints are stored as exact savestate roots and may become the next committed search root.
  - A small committed-root stack is retained for rollback if later segments fail.

Node Shape:  
  - Each search node carries an exact emulator savestate.
  - Each search node also carries a reduced evaluator summary sufficient to preserve SMB search semantics without embedding the full evaluator object.
  - The reduced evaluator summary includes at least:
      - best frontier so far
      - gameplay frames elapsed
      - gameplay frames since frontier improvement
      - terminal status
  - Each node stores parent linkage and the action taken from the parent, so the final Plan can be reconstructed exactly.
  - Each node may cache coarse derived features for scoring and pruning, such as:
      - current frontier
      - motion context
      - velocity
      - local hazard context
      - checkpoint eligibility
  - Beam identity, bucket policy, and ranking score are derived from node state and cached features; they are not the same thing as the node’s correctness-bearing
    state.
  - Vertical/platform bucketing is intentionally excluded from the initial beam policy. It may be added later as an optional diversity mechanism if route selection
    becomes too narrow.

  Action space:
  - Search branches on one fixed-timestep action per frame.
  - The initial SMB search action space is a curated legal-action set, not the full controller product.
  - The initial SMB legal-action set includes forward, backward-running, jump-running, ducking, and crouch-jump-running actions.
  - In particular, the initial set includes:
      - neutral
      - right
      - right + run
      - right + jump
      - right + jump + run
      - left + run
      - left + jump + run
      - duck
      - duck + jump
      - duck + right + jump + run
      - duck + left + jump + run
  - Start, Select, X, and Y are excluded from the initial SMB search action set.
  - The legal SMB action set is represented explicitly and mapped to PlayerControlFrame; Search does not branch over arbitrary raw controller combinations.
  - Successor ordering may still prefer more likely actions first, but all legal actions remain available to the search.

Search algorithm:

  - The initial SMB search algorithm is segment-oriented deterministic candidate search.
  - Search starts from a committed exact-savestate checkpoint root.
  - For each segment, Search enumerates candidate action sequences in a fixed order:
      - candidate `0` is baseline (`RightRun` in every decision slot)
      - remaining candidates are ordered by increasing deviation count from baseline
      - ties are broken by the fixed SMB action ordering
  - Segment configuration separates:
      - `segmentFrameBudget` (total frames explored per candidate)
      - `searchDepth` (number of decision slots in the segment)
  - Each candidate sequence is rolled out from the current checkpoint root to completion (or terminal).
  - Candidate success is based on evaluator score increase over the root:
      - endpoint must be non-terminal gameplay state
      - endpoint evaluation score must be strictly greater than the root score
  - First successful candidate at a checkpoint is promoted immediately.
  - If all candidates for a checkpoint fail, Search backtracks to the prior committed checkpoint and resumes from its next candidate index.
  - Search is single-threaded in this phase.
  - Transposition tables and parallel expansion are deferred.

Pruning/scoring policy
  - The initial pruning policy is intentionally minimal.
    - prune terminal/evaluator-terminal outcomes
    - reject non-gameplay endpoints for checkpoint promotion
  - The initial implementation does not yet use:
    - transposition pruning
    - dominance pruning
    - optimistic bound pruning
  - Candidate ordering is deterministic and mostly heuristic-free beyond baseline-first ordering.
  - Promotion uses evaluator score improvement over root as the primary success rule.

Segment success/failure policy
   
  - A search session is a sequence of segment attempts from committed roots.
  - A segment attempt succeeds when the first promotable endpoint is found for the current checkpoint.
  - Promotion stores the exact savestate root and segment `Plan` fragment.
  - A segment attempt fails when all candidates for the current checkpoint are exhausted without a promotable endpoint.
  - On segment failure, Search backtracks to the previous committed checkpoint and resumes from its next untried candidate.
  - The search session ends when:
    - max segment limit is reached (unless unlimited), or
    - root checkpoint is exhausted (no further progress), or
    - user stop/error.

Live Search Rendering:
  - Current implementation renders the active candidate endpoint frame, not every internal rollout frame.
  - Planned next step:
    - keep search running continuously
    - on a wall-clock preview interval, emit the current live rollout frame
    - do not retain historical frame buffers
  - This requires making candidate rollout interruptible/resumable so Search can yield preview frames without waiting for full candidate completion.

Test fixture strategy.
- Search implementation should be developed against fixed exact-savestate SMB root fixtures.
- Early search tests are micro-benchmarks, not full-level tests.
- Initial fixture targets should include at least:
    - flat ground sanity
    - first goomba
    - first gap
- Initial automated checks should cover:
    - deterministic results from fixed root and parameters
    - successful frontier gain on simple fixtures
    - clean failure on insufficient budget
    - exact plan replay from the root savestate
    - checkpoint eligibility of promoted roots
- End-to-end UI/functional tests are still useful, but they are secondary to fixed-root search-core tests for early search development.
- Use screenshots as needed.

Implementation plan:
1. Fixed-root fixture and replay foundation.

  - add exact SMB savestate fixtures for at least:
      - flat ground sanity
      - first goomba
      - first gap
  - add a small harness to load a root savestate, replay a Plan, and report evaluator summary
  - add tests for deterministic replay from fixed roots

2. Search core data model.

  - add SearchNode
  - add reduced evaluator summary
  - add explicit SMB legal-action enum/set mapped to PlayerControlFrame
  - include the initial action set from day one:
      - forward
      - backward-running
      - jump-running
      - ducking
      - crouch-jump-running
  - add tests for action mapping and plan reconstruction

3. Single-threaded deterministic segment search.

  - implement one segment attempt from one exact root
  - deterministic candidate enumeration over fixed action ordering
  - explicit `searchDepth` and `segmentFrameBudget`
  - first-success promotion policy
  - no transposition table
  - add micro-benchmark tests for:
    - determinism
    - baseline progression
    - dead-end candidate exhaustion
    - backtrack and resume from parent checkpoint

4. Checkpoint promotion and committed-root flow.

  - add checkpoint-eligibility filter using evaluator score increase
  - promote first successful candidate at each checkpoint
  - store promoted checkpoint as exact savestate root plus segment `Plan`
  - keep a committed-root stack with per-checkpoint next-candidate cursor
  - failure policy: backtrack and resume from prior checkpoint cursor
  - add tests for checkpoint eligibility, exhaustion, and backtrack correctness

5. Search runner integration.

  - plug the segment-search runner into SearchActive
  - keep the current Search UI shell
  - expose truthful progress for the real segment search
  - expose completion reason and completion-state controls (`PLAY`, `BACK`)
  - add at least one CLI/functional smoke test for a real segment search path

6. Performance and search-quality follow-ups.

  - exact transpositions
  - dominance / bucket policy
  - hazard-aware widening
  - richer checkpoint ranking beyond first-success
  - rollout preview emission on wall-clock interval while search is in progress
  - parallel expansion


### Phase 3: Generalize to support Clock Scenario and Duck

Needs planning

### Phase 4: Generalize to support NES Flappy bird

Needs planning
