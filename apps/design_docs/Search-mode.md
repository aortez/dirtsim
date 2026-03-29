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
- `Scenario setup`: a deterministic startup preset or procedure that brings a scenario to the desired starting state for a run, such as `player1_gameplay_1_1` for SMB.
- `Training`: a learning workflow that evaluates populations and produces genomes.
- `Search`: a search workflow that evaluates candidate trajectories or states and produces one or more plans, traces, or ranked outcomes.
- `Plan`: the action sequence or control trace that Search returns as its current best answer.
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
- Reuse the scenario setup logic from nes training.  Later: reuse the duck spawn logic from clock scenario training... tree spawn logic from tree training, etc...
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
- owns search worker execution
- owns progress broadcast cadence
- owns best playback or best snapshot emission
- owns result persistence on completion or stop
- allows for pausing/resume of search

`PlanPlayback`

- plays back a saved plan with its attached scenario and player/organism setup
- owns playback timing, pause state, and playback frame emission
- returns to `Idle` when stopped or when playback completes

## Execution Model

- `search workers` evaluate states or action sequences using search runners.
Visualization enabled for active search and whether the best playback are enabled are Search options.
- `playback runner` replays the current best candidate for the UI

This mirrors an existing training pattern where the active UI shows a live best playback rather than every raw evaluation.

Phase 1 implementation:

- one search session
- one best-playback runner
- one stream of search progress

## Shared Infrastructure With Training

Search should not reuse the current training states directly, but it should reuse infrastructure below the state boundary where practical. 

Extract reusable pieces when search would otherwise copy existing training code wholesale.

Candidate shared pieces:

- render stream subscription setup
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
    uint64_t nodesExpanded = 0;
    uint64_t uniqueStates = 0;
    uint64_t transpositionHits = 0;
    std::optional<double> bestFrontier = std::nullopt;
};
```

### Plans


Needs planning:
```cpp
struct Plan {
    Scenario::EnumType scenarioId;
    std::vector<InputAction> inputs;
};
```

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

### Server -> UI Broadcasts

- `SearchProgress`
- `SearchBestSnapshot`
- `SearchBestPlaybackFrame`
- `PlanSaved` (completion)

### Plan Repository

Plans need their own repository: `PlanRepository`

## Phased Rollout

### Phase 1: Search shell

- add `SearchIdle`, `SearchActive`, `PlanPlayback`
- add Search start/stop/pause/progress APIs
- add dedicated Plan types and repository
- minimal Search config panel and active screen
- support `NesSuperMarioBros` only
- brain dead search - hold right

Success criteria:

- Search is reachable from `StartMenu`
- Search can run, stream progress, and stop cleanly
- Plans are persisted and browsable
- brain dead search - run right until death by first goomba
- Playback can play back saved Plan

### Phase 2: Basic Search implementation.

Needs planning

### Phase 3: Generalize to support Clock Scenario and Duck

Needs planning

### Phase 4: Generalize to support NES Flappy bird

Needs planning
