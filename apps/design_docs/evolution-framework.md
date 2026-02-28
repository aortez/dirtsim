# Evolution Framework

## Overview

This document describes the architecture for coordinating neural network evolution across the application. It covers state machines, the genome repository, API commands, and persistence.

For the genetic algorithm details (selection, mutation, fitness), see `genetic-evolution.md`.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                           Server                                 │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                   GenomeRepository                         │  │
│  │              (persists across all states)                  │  │
│  │                                                           │  │
│  │   • Store/retrieve genomes                                │  │
│  │   • Save/load to disk (binary or JSON)                    │  │
│  │   • Track best genome from training                       │  │
│  │   • Share via API                                         │  │
│  └───────────────────────────────────────────────────────────┘  │
│         ▲              ▲              ▲                         │
│         │              │              │                         │
│  ┌──────┴──────┐ ┌─────┴─────┐ ┌──────┴──────┐                 │
│  │  StateIdle  │ │  State    │ │  State      │                 │
│  │             │ │ Evolution │ │ SimRunning  │                 │
│  │ • Query     │ │           │ │             │                 │
│  │ • Import    │ │ • Create  │ │ • Load      │                 │
│  │ • Export    │ │ • Store   │ │ • Spawn     │                 │
│  └─────────────┘ └───────────┘ └─────────────┘                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
          ▲
          │ WebSocket API
          ▼
┌─────────────────────────────────────────────────────────────────┐
│                            UI                                    │
│                                                                 │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐           │
│  │  State      │   │  State      │   │  State      │           │
│  │ StartMenu   │──▶│  Training   │──▶│ SimRunning  │           │
│  │             │   │             │   │             │           │
│  │ • Choose    │   │ • Progress  │   │ • View tree │           │
│  │   mode      │   │ • Stats     │   │ • Interact  │           │
│  │             │   │ • Controls  │   │             │           │
│  └─────────────┘   └─────────────┘   └─────────────┘           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## GenomeRepository

A service that lives at the Server level, persisting across state changes.

### Interface

See `src/core/organisms/evolution/GenomeRepository.h` and `GenomeMetadata.h`.

Key points:
- `GenomeId` is a UUID (RFC 4122 v4), caller-provided
- `store(id, genome, meta)` overwrites if ID exists
- Persistence methods not yet implemented

### Usage Patterns

**During evolution:**
```cpp
// StateEvolution stores promising genomes periodically.
if (generation % 10 == 0) {
    GenomeMetadata meta{
        .name = "gen_" + std::to_string(generation) + "_best",
        .fitness = best_fitness,
        .generation = generation,
        .created_timestamp = now(),
        .scenario_id = config.scenario_id
    };
    GenomeId id = repository.store(best_genome, meta);

    if (best_fitness > all_time_best) {
        repository.markAsBest(id);
    }
}
```

**Loading for simulation:**
```cpp
// StateSimRunning loads genome to spawn tree.
if (auto genome = repository.get(requested_id)) {
    auto brain = std::make_unique<NeuralNetBrain>(*genome);
    tree_manager.plantSeedWithBrain(position, std::move(brain));
}
```

## Server State Machine

### States

```
StateIdle
    │
    ├── SimRun ──────────────▶ StateSimRunning
    │                              │
    │                              ├── SimPause ──▶ StateSimPaused
    │                              │                    │
    │                              │   SimResume ◀──────┘
    │                              │
    │                              └── SimStop ──▶ StateIdle
    │
    └── EvolutionStart ──────▶ StateEvolution
                                   │
                                   ├── EvolutionPause ──▶ StateEvolutionPaused
                                   │                          │
                                   │   EvolutionResume ◀──────┘
                                   │
                                   ├── EvolutionStop ──▶ StateIdle
                                   │
                                   └── (completed) ──▶ StateIdle
```

### StateEvolution

Runs the genetic algorithm loop.

```cpp
class StateEvolution : public State {
public:
    void enter() override {
        // Initialize population.
        population_ = initializePopulation(config_.population_size);
        generation_ = 0;
        current_eval_ = 0;
    }

    void update() override {
        if (generation_ >= config_.max_generations) {
            // Store final best and transition to Idle.
            storeBestGenome();
            requestTransition(StateIdle);
            return;
        }

        if (current_eval_ < population_.size()) {
            // Evaluate next individual.
            evaluateOne(current_eval_);
            current_eval_++;
            broadcastProgress();
        } else {
            // Generation complete - select, mutate, replace.
            advanceGeneration();
            generation_++;
            current_eval_ = 0;
        }
    }

private:
    EvolutionConfig config_;
    std::vector<Genome> population_;
    std::vector<double> fitness_;
    int generation_;
    int current_eval_;
    GenomeRepository& repository_;
};
```

## UI State Machine

### States

```
StateDisconnected
    │
    └── (connected) ──▶ StateStartMenu
                            │
                            ├── "Run Simulation" ──▶ StateSimRunning
                            │                            │
                            │                            └── back ──▶ StateStartMenu
                            │
                            └── "Train Evolution" ──▶ StateTraining
                                                         │
                                                         ├── "View Best" ──▶ StateSimRunning
                                                         │                       │
                                                         │                       └── back
                                                         │
                                                         └── "Stop" ──▶ StateStartMenu
```

### StateTraining UI Layout

```
┌─────────────────────────────────────────────────────────────┐
│  [⏸ Pause] [⏹ Stop]              EVOLUTION                  │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Generation: 17 / 100                                      │
│   ████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░  34%          │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐   │
│   │  Evaluating: 23 / 50                                │   │
│   │  ██████████████████████░░░░░░░░░░░░░░░░░░  46%      │   │
│   └─────────────────────────────────────────────────────┘   │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Best (this gen):   2.34                                   │
│   Best (all time):   2.89     [👁 View]  [💾 Save]          │
│   Average:           1.12                                   │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ┌───────────────────┐                                     │
│   │                   │   Best Tree Preview                 │
│   │    (mini world    │   Generation 14                     │
│   │     preview)      │   Fitness: 2.89                     │
│   │                   │   Cells: 47                         │
│   └───────────────────┘   Lifespan: 582s                    │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## API Commands

### Evolution Control

```cpp
// Start evolution training.
struct EvolutionStart {
    EvolutionConfig evolution;   // Population size, generations, etc.
    MutationConfig mutation;     // Mutation rate, sigma.
    std::string scenario_id;     // Nursery scenario to use.
};
struct EvolutionStartResponse {
    bool success;
    std::string error;           // If failed.
};

// Pause/resume.
struct EvolutionPause {};
struct EvolutionResume {};
struct EvolutionStop {};

// Progress updates (server → UI, periodic broadcast).
struct EvolutionProgress {
    int generation;
    int max_generations;
    int current_eval;
    int population_size;
    double best_fitness_this_gen;
    double best_fitness_all_time;
    double average_fitness;
    GenomeId best_genome_id;
};

// Get current status.
struct EvolutionStatusGet {};
struct EvolutionStatusResponse {
    bool running;
    bool paused;
    EvolutionProgress progress;  // Current state.
};
```

### Genome Repository

See `src/server/api/Genome*.h` for full definitions.

| Command | Description | Status |
|---------|-------------|--------|
| `GenomeSet` | Store genome with caller-provided UUID | ✅ Implemented |
| `GenomeGet` | Retrieve genome by ID | ✅ Implemented |
| `GenomeList` | List all stored genomes | ✅ Implemented |
| `GenomeDelete` | Remove genome by ID | ❌ Not yet |
| `GenomeSave` | Save repository to disk | ❌ Not yet |
| `GenomeLoad` | Load repository from disk | ❌ Not yet |

### Running Simulation with Specific Genome

The `TreeGerminationConfig` includes a `genome_id` field. When set, `TreeGerminationScenario::setup()` looks up the genome in `GenomeRepository` and creates a `NeuralNetBrain` from it.

**Flow for "View Best" from Training state:**
```
SimRun{scenario=TreeGermination}
UserSettingsPatch{treeGerminationScenarioConfig=TreeGermination{genome_id=<uuid>}}
Reset{}
```

The Reset re-runs `setup()` which now uses the genome_id from the updated config.

| Command | Description | Status |
|---------|-------------|--------|
| `UserSettingsPatch` | Update user settings (including persisted scenario config) | ✅ Implemented |
| `Reset` | Re-run scenario setup with current config | ✅ Implemented |

## Persistence

### Binary Format (Primary)

Efficient storage for large weight vectors.

```
Header (16 bytes):
    magic: "GNME"        (4 bytes)
    version: uint32      (4 bytes)
    genome_count: uint32 (4 bytes)
    best_id: uint32      (4 bytes, 0 = none)

For each genome:
    id: uint32           (4 bytes)
    metadata_length: uint32
    metadata: JSON string (variable)
    weight_count: uint32 (4 bytes)
    weights: double[]    (weight_count * 8 bytes)
```

~1 MB per genome (120K weights × 8 bytes + metadata).

### JSON Format (Interchange)

Human-readable, useful for debugging and sharing.

```json
{
    "version": 1,
    "best_id": 42,
    "genomes": [
        {
            "id": 42,
            "metadata": {
                "name": "best_gen_47",
                "fitness": 2.89,
                "generation": 47,
                "created_timestamp": 1704931200,
                "scenario_id": "calm_nursery",
                "notes": "First successful seed producer"
            },
            "weights": [0.023, -0.017, 0.089, ...]
        }
    ]
}
```

### Auto-Save Behavior

```cpp
// Server saves repository periodically and on shutdown.
void Server::shutdown() {
    genome_repository_.saveBinary(getDataPath() / "genomes.bin");
}

void Server::initialize() {
    auto path = getDataPath() / "genomes.bin";
    if (std::filesystem::exists(path)) {
        genome_repository_.loadBinary(path);
    }
}
```

## Flow Examples

### Training Session

```
1. User selects "Train Evolution" from StartMenu
2. UI sends EvolutionStart to server
3. Server transitions to StateEvolution
4. Server broadcasts EvolutionProgress periodically
5. UI displays progress in StateTraining

6. Every 10 generations, server stores best genome in repository

7. User clicks "View Best"
   a. UI sends EvolutionStop (if still running)
   b. UI sends SimRun{scenario=TreeGermination}
   c. UI sends UserSettingsPatch{treeGerminationScenarioConfig=TreeGermination{genome_id=best_genome_id}}
   d. UI sends Reset (re-runs setup with genome)
   e. UI transitions to SimRunning state
   f. User watches tree grow

8. Evolution completes or user clicks "Stop"
   a. Server stores final best genome
   b. Server transitions to StateIdle
   c. UI transitions to StateStartMenu
```

### Loading Saved Genome

```
1. User selects "Run Simulation" from StartMenu
2. UI shows scenario selector with "Load Genome" option
3. User clicks "Load Genome"
4. UI sends GenomeList
5. UI displays list with fitness/generation info
6. User selects a genome
7. UI sends SimRun with genome_id
8. Tree spawns with selected brain
```

### Sharing a Genome

```
1. User has trained a successful genome
2. User sends GenomeExport { id: 42, binary: false }
3. Server returns JSON string
4. User copies/pastes to friend
5. Friend sends GenomeImport { data: "...", name: "friend_tree" }
6. Genome now in friend's repository
```

## File Locations

```
~/.dirtsim/
├── genomes.bin          # Primary repository (auto-saved).
├── genomes/             # Individual exports.
│   ├── best_tree.genome
│   └── experiment_1.genome
└── evolution_logs/      # Training history.
    └── 2024-01-11_training.log
```

## Implementation Plan

### Directory Structure

```
src/core/organisms/evolution/
├── EvolutionConfig.h         # Config structs (EvolutionConfig, MutationConfig)
├── FitnessResult.h           # Fitness inputs (lifespan, max_energy)
├── GenomeMetadata.h          # Metadata struct (fitness, generation, timestamp)
├── GenomeRepository.h/cpp    # Storage, CRUD, persistence
├── Mutation.h/cpp            # Gaussian perturbation, weight reset
├── Selection.h/cpp           # Tournament selection
├── TrainingRunner.h/cpp      # Incremental genome evaluation (non-blocking)
└── tests/
    ├── GenomeRepository_test.cpp
    ├── Mutation_test.cpp
    ├── Selection_test.cpp
    └── TrainingRunner_test.cpp

src/core/scenarios/
├── ScenarioRegistry.h/cpp    # Scenario factory (moved from server/)

src/server/states/
├── Evolution.h/cpp           # Main evolution loop state
├── EvolutionPaused.h/cpp     # Paused state
└── (existing states...)

src/server/tests/
└── StateEvolution_test.cpp   # Evolution state tests

src/server/api/
├── EvolutionStart.h          # Start evolution command
├── EvolutionPause.h          # Pause command
├── EvolutionResume.h         # Resume command
├── EvolutionStop.h           # Stop command
├── GenomeList.h              # List genomes command
├── GenomeGet.h               # Get specific genome
└── (existing commands...)

src/ui/state-machine/states/
└── Training.h/cpp            # Training UI state
```

### Phase 1: Core Data Types ✅

**Files:**
- `EvolutionConfig.h` ✅
- `MutationConfig.h` (in EvolutionConfig.h) ✅
- `FitnessResult.h` ✅
- `GenomeMetadata.h` ✅

**Work:**
- Define config structs with defaults from `genetic-evolution.md`.
- Define metadata struct for genome tracking.
- GenomeId strong type for type-safe genome identification.
- Pure data, no logic.

**Tests:** None needed (data-only structs).

### Phase 2: GenomeRepository ✅

**Files:**
- `GenomeRepository.h/cpp` ✅
- `tests/GenomeRepository_test.cpp` ✅ (10 tests)

**Work:**
- In-memory storage (map of GenomeId → Genome + Metadata).
- CRUD operations: store, get, getMetadata, list, remove, clear.
- Best tracking: markAsBest, getBestId, getBest.
- Count/empty helpers.

**Tests:**
- Store and retrieve genome. ✅
- List returns all stored genomes. ✅
- Remove deletes genome. ✅
- Best tracking works correctly. ✅
- Clear removes all genomes. ✅

**Persistence deferred to Phase 7.**

### Phase 3: Evolution Algorithms ✅

**Files:**
- `Mutation.h/cpp` ✅
- `Selection.h/cpp` ✅
- `tests/Mutation_test.cpp` ✅ (4 tests)
- `tests/Selection_test.cpp` ✅ (5 tests)

**Work:**
- `mutate(genome, config, rng)` — Gaussian perturbation + rare reset. ✅
- `tournamentSelect(population, fitness, k, rng)` — pick k, return best. ✅
- `elitistReplace(parents, offspring, fitness, size)` — keep top N. ✅
- `computeFitness(result)` — in FitnessResult (Phase 1). ✅

**Tests:**
- Mutation changes weights within expected distribution. ✅
- Mutation with rate=0 produces identical genome. ✅
- Tournament always returns element from population. ✅
- Tournament with k=population_size returns best. ✅
- Elitist replace keeps top genomes sorted by fitness. ✅

### Phase 3.5: TrainingRunner ✅

Incremental genome evaluation that doesn't block the main thread.

**Problem solved:**
The original `evaluateGenome()` ran a tight `while` loop for up to 10 minutes,
blocking all event processing. No cancel, no pause, no rendering during training.

**Solution:**
TrainingRunner owns a World and steps it incrementally via `step(frames)`.
Control returns to caller between steps, allowing event processing and rendering.

**Files:**
- `src/core/organisms/evolution/TrainingRunner.h/cpp` ✅
- `src/core/organisms/evolution/tests/TrainingRunner_test.cpp` ✅ (2 tests)

**Interface:**
```cpp
class TrainingRunner {
    TrainingRunner(const Genome& genome, Scenario::EnumType scenarioId,
                   const EvolutionConfig& config);

    Status step(int frames = 1);  // Returns Running, TreeDied, or TimeExpired.

    const World* getWorld() const;  // Access for rendering.
    double getSimTime() const;
    double getMaxTime() const;
    float getProgress() const;
};
```

**Tests:**
- `StepIsIncrementalNotBlocking` — step() returns quickly, world accessible. ✅
- `CompletionReturnsFitnessResults` — full lifecycle returns metrics. ✅

**TODO:** Wire into Evolution state to replace blocking `evaluateGenome()`.

### Phase 4: Server StateEvolution ✅

**Files:**
- `src/server/states/Evolution.h/cpp` ✅
- `src/server/states/EvolutionPaused.h/cpp` ❌ (deferred - no pause/resume yet)
- `src/server/api/EvolutionStart.h/cpp` ✅
- `src/server/api/EvolutionStop.h/cpp` ✅
- `src/server/api/EvolutionProgress.h/cpp` ✅
- `src/server/tests/StateEvolution_test.cpp` ✅ (6 tests)

**Work:**
- StateEvolution: ✅ (refactoring in progress)
  - Initialize random population on enter. ✅
  - ~~Run evaluation loop (one organism per tick, blocking). ✅~~
  - **REFACTORING**: Original implementation blocked main thread for entire evaluation.
    Now using TrainingRunner for incremental, non-blocking evaluation.
  - Create fresh World using ScenarioRegistry. ✅
  - Run simulation for up to 10 minutes sim time. ✅
  - Collect FitnessResult, compute fitness. ✅
  - After full generation: select, mutate, replace. ✅
  - Store best genome in repository periodically. ✅
  - Broadcast EvolutionProgress with serialized data. ✅
  - Transition to Idle on completion or EvolutionStop. ✅
- StateEvolutionPaused: ❌ (deferred)
- Hook GenomeRepository into Server class (member, lives across states). ✅
- CLI watch command for monitoring broadcasts. ✅
- EvolutionStart/Stop wired to CLI dispatcher and StateMachine handlers. ✅

**Tests:** ✅
- EvolutionStart transitions Idle → Evolution. ✅
- EvolutionStop transitions Evolution → Idle. ✅
- Tick evaluates organisms, advances generation. ✅
- Best genome stored in repository after generation. ✅
- Completes all generations and transitions to Idle. ✅
- Exit command transitions to Shutdown. ✅

### Phase 5: API Commands ✅

**Files:**
- `src/server/api/EvolutionStart.h` ✅
- `src/server/api/EvolutionPause.h` ❌ (deferred with pause/resume)
- `src/server/api/EvolutionResume.h` ❌ (deferred with pause/resume)
- `src/server/api/EvolutionStop.h` ✅
- `src/server/api/GenomeList.h` ✅
- `src/server/api/GenomeGet.h` ✅

**Work:**
- Define command/response structs following existing API pattern. ✅
- Wire into server command dispatch. ✅
- Evolution commands route to StateEvolution. ✅
- Genome commands access GenomeRepository (available from any state). ✅
- All genome commands implemented as global handlers. ✅

**Tests:**
- Covered by StateEvolution tests. ✅

### Phase 6: UI StateTraining (in progress)

**Files:**
- `src/ui/state-machine/states/Training.h/cpp` ✅
- `src/ui/state-machine/tests/StateTraining_test.cpp` ✅ (3 tests)

**Work:**
- New UI state entered via "Train" from StartMenu. ✅
  - Added Train button (ActionButton style) to StartMenu center-right.
  - Added TrainButtonClickedEvent to UI event system.
  - StartMenu transitions to Training on button click.
- Added StateMachine::TestMode for unit testing UI states. ✅
- Subscribe to EvolutionProgress broadcasts from server. ✅
- Render training UI: ✅
  - Generation progress bar.
  - Current evaluation progress bar.
  - Best fitness (this gen, all time).
  - Average fitness.
  - Mini preview of current tree (live world view).
- Controls: Start, Stop, Quit buttons. ✅
- "View Best" button (stops evolution, transitions to SimRunning with genome). ✅

**Tests:**
- State transitions correctly on server events. ✅ (3 tests)
  - TrainButtonClicked transitions StartMenu → Training.
  - Exit command transitions Training → Shutdown.
  - State has correct name "Training".
- Progress display updates on EvolutionProgress. ❌
- Pause/Stop send correct commands. ❌

### Phase 7: Persistence

**Files:**
- Extend `GenomeRepository.h/cpp`
- `tests/GenomeRepository_test.cpp` (add persistence tests)

**Work:**
- `saveBinary(path)` / `loadBinary(path)` — efficient storage.
- `saveJson(path)` / `loadJson(path)` — human-readable interchange.
- Auto-save on server shutdown.
- Auto-load on server startup.

**Binary format:**
```
Header: magic(4) + version(4) + count(4) + best_id(4)
Per genome: id(4) + metadata_len(4) + metadata(JSON) + weight_count(4) + weights(double[])
```

**Tests:**
- Save and load round-trip preserves all data.
- Load nonexistent file returns gracefully.
- Corrupt file handled gracefully.

### Phase 8: Integration & Polish

**Work:**
- TreeGerminationConfig includes genome_id field. ✅
- UserSettingsPatch persists scenario config (including genome_id). ✅
- "View Best" flow: stop evolution → SimRun → UserSettingsPatch → Reset. ✅
- Scenario selector for training (default: tree_germination). ✅
- Resume training from saved population (optional, can defer). ❌
- Error handling and edge cases. ❌

**Tests:**
- UserSettingsPatch + Reset spawns tree with correct brain. (manual testing) ✅
- Full training → view flow works end-to-end. (manual testing) ✅

### Dependencies

```
Phase 1 ─────┬─────▶ Phase 2 ─────┬─────▶ Phase 4 ─────▶ Phase 5 ─────▶ Phase 6
             │                    │           │
             └─────▶ Phase 3 ─────┘           │
                                              ▼
                                          Phase 7 ─────▶ Phase 8
```

Phases 2 and 3 can proceed in parallel after Phase 1.

## Related Documents

- `genetic-evolution.md` - Algorithm details (selection, mutation, fitness).
- `plant.md` - Tree organism and brain interface.
- `neural-net-brain-plan.md` - Neural network architecture.
