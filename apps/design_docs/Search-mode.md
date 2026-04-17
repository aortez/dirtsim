# Search Mode

## Status

Living implementation planning document.

Phase 1 is implemented. Phase 2 has an SMB1 DFS implementation in progress, with the
current investigation focused on why the search reaches the first pit but does not yet
find a stable way past it.

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

During Phase 2, `SearchActive` renders the node currently being expanded by the live search.
This view is intended to reflect the actual traversal, so backtracking and branch changes may be
visible. The initial implementation may emit a render update for every searched frame. Later
phases may decouple render cadence from search cadence and throttle visual updates to a
configurable interval without changing the underlying search behavior. Saved-plan replay remains a
separate `PlanPlayback` workflow rather than a continuous overlay during active search.

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
- `search runner` also emits the currently rendered search state for `SearchActive`
- `playback runner` replays a saved plan during `PlanPlayback`

Phase 1 implementation:

- one search session
- one stream of search progress
- `SearchStart` is empty for Phase 1
- Search uses the same scenario initialization path and SMB evaluator path as training
- the Phase 1 search policy is fixed: emit `PlayerControlFrame{ .xAxis = 127 }` for every frame
- each advanced gameplay frame appends one frame to the candidate `Plan`
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

Phase 2 ships with an SMB-specific adapter and runner behind the general Search workflow.
The tree search structure is intended to remain general even though the initial legal actions,
fixtures, and rendering path are SMB-specific.

Non-NES scenarios will have an associated `OrganismType`, such as duck, tree, or goose.

## Search Capability Discovery

Scenario discovery is deferred.

Phase 1 and Phase 2 do not add a dedicated search-catalog or search-scenarios API.
The initial Search workflow is launched against the currently supported Search target.

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

- `SearchStart`
- `SearchPauseSet`
- `SearchStop`
- `SearchProgressGet`
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
- `PlanSaved` (completion)
- `SearchCompleted`
- `PlanPlaybackStopped`

Phase 1 shapes:

```cpp
using PlanSaved = PlanSummary;

enum class SearchCompletionReason {
    Completed,
    Stopped,
    Error,
};

struct SearchCompleted {
    SearchCompletionReason reason = SearchCompletionReason::Completed;
    std::optional<PlanSummary> summary = std::nullopt;
    std::string errorMessage;
};

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
- `SearchCompleted` broadcasts terminal search status and the saved summary when available
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

Replace the Phase 1 hold-right runner with a real frame-by-frame SMB searcher.

- Search operates over a gameplay tree, with one gameplay frame per node.
- Each node stores the runtime savestate needed to continue searching from that node, plus the evaluator summary,
parent link, and action-from-parent.
- Phase 2 uses deterministic depth-first traversal with backtracking.
- At each node, legal actions are tried in a deterministic per-node order. The ordering can depend on the parent
action and the current SMB motion state.
- When an action produces a live child, search descends into that child.
- When an action produces a terminal, dead, or unrecoverable child, that child is pruned immediately.
- When all legal actions from a node have been exhausted, search backtracks to the parent and tries the next legal
action there.
- A live branch may also be pruned when it hits a configurable `framesSinceProgress` stall limit, gets stuck with
no horizontal velocity against an obstacle, or falls below a configurable screen-space threshold.
- The current best `Plan` is reconstructed by walking parent links from the best leaf seen so far back to the root.
- Surviving branches are compared primarily by frontier progress, with evaluation score used as a secondary signal.
- Phase 2 intentionally does not introduce checkpoints, segments, or user-facing search-depth controls.
- Search remains deterministic for a fixed root state and fixed option set.
- Phase 2 continues to support `NesSuperMarioBros` only.

Search completion for Phase 2 distinguishes between finding progress and exhausting the search:

- the user stops the search
- no live branches remain
- an implementation-defined global search budget is exhausted
- a target milestone is reached, such as getting past the first goomba or another fixture-defined progress target

Phase 2 debugging support:

- emit a search trace that records node id, parent id, depth, action, frontier, evaluation score,
`framesSinceProgress`, motion-priority metadata, and prune or completion reason
- use simple SMB1 roots such as flat ground, first goomba, first pipe, and first pit as repeatable debug entry points
- ensure the same root and settings produce the same trace and the same selected best plan
- write diagnostic JSONL traces and PNG screenshots from disabled tests when investigating search-tree behavior

Success criteria:

- Search can branch and backtrack from stored savestates.
- Search can discover at least one non-trivial plan from an early SMB1 root.
- Search can get past the first goomba from a repeatable early SMB1 debug root.
- Search produces a deterministic trace for the same root and settings.
- The best leaf can be reconstructed into a saved `Plan` and replayed through existing plan playback.
- The debug trace is sufficient to explain why branches were expanded, pruned, or selected as best.

Current implementation status:

- DFS search, savestate backtracking, deterministic traces, saved-plan reconstruction, and `PlanPlayback` replay
from boot are implemented.
- The current SMB search can get past the first goomba and early pipe with the implemented action-ordering and
pruning heuristics.
- The current hotspot is the first pit. The search reaches the pit quickly, and below-screen pruning prevents
continuing to search far off-screen alive-but-doomed states.
- The first-pit diagnostic trace shows that DFS can still spend budget enumerating equivalent late-fall tails before
it backtracks far enough to try a useful jump near the ledge.
- Likely next work is falling-state transposition or dominance pruning.

### Phase 3: Generalize to support Clock Scenario and Duck

Needs planning

### Phase 4: Generalize to support NES Flappy bird

Needs planning
