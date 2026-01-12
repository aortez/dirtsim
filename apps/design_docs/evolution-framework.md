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

```cpp
using GenomeId = uint32_t;

struct GenomeMetadata {
    std::string name;           // User-provided or auto-generated.
    double fitness;             // Best fitness achieved.
    int generation;             // Generation it came from.
    uint64_t created_timestamp; // Unix timestamp.
    std::string scenario_id;    // Which scenario it was trained on.
    std::string notes;          // Optional user notes.
};

class GenomeRepository {
public:
    // Store and retrieve.
    GenomeId store(const Genome& genome, const GenomeMetadata& meta);
    std::optional<Genome> get(GenomeId id) const;
    std::optional<GenomeMetadata> getMetadata(GenomeId id) const;
    std::vector<std::pair<GenomeId, GenomeMetadata>> list() const;
    void remove(GenomeId id);
    void clear();

    // Persistence.
    void saveBinary(const std::filesystem::path& path);
    void loadBinary(const std::filesystem::path& path);
    void saveJson(const std::filesystem::path& path);
    void loadJson(const std::filesystem::path& path);

    // Best genome tracking.
    void markAsBest(GenomeId id);
    std::optional<GenomeId> getBestId() const;
    std::optional<Genome> getBest() const;

    // Statistics.
    size_t count() const;
    bool empty() const;

private:
    std::unordered_map<GenomeId, Genome> genomes_;
    std::unordered_map<GenomeId, GenomeMetadata> metadata_;
    std::optional<GenomeId> best_id_;
    GenomeId next_id_ = 1;
};
```

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

```cpp
// List all stored genomes.
struct GenomeList {};
struct GenomeListResponse {
    std::vector<std::pair<GenomeId, GenomeMetadata>> genomes;
};

// Get a specific genome.
struct GenomeGet {
    GenomeId id;
};
struct GenomeGetResponse {
    Genome genome;
    GenomeMetadata metadata;
};

// Store a genome (import).
struct GenomeStore {
    Genome genome;
    GenomeMetadata metadata;
};
struct GenomeStoreResponse {
    GenomeId id;
};

// Delete a genome.
struct GenomeDelete {
    GenomeId id;
};

// Get current best.
struct GenomeGetBest {};
struct GenomeGetBestResponse {
    bool found;
    GenomeId id;
    Genome genome;
    GenomeMetadata metadata;
};

// Save/load repository to disk.
struct GenomeSave {
    std::string path;
    bool binary;  // True for binary, false for JSON.
};
struct GenomeLoad {
    std::string path;
};

// Export single genome for sharing.
struct GenomeExport {
    GenomeId id;
    bool binary;
};
struct GenomeExportResponse {
    std::string data;  // Base64-encoded binary or JSON string.
};

// Import single genome.
struct GenomeImport {
    std::string data;
    std::string name;
};
struct GenomeImportResponse {
    GenomeId id;
};
```

### Running Simulation with Specific Genome

```cpp
// Extended SimRun to support loading a genome.
struct SimRun {
    std::string scenario_id;
    std::optional<GenomeId> genome_id;  // If set, use this genome for tree brain.
    // ... existing fields ...
};
```

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
   a. UI sends EvolutionPause
   b. UI sends GenomeGetBest
   c. UI sends SimRun with genome_id
   d. Server transitions to StateSimRunning (evolution paused)
   e. User watches tree grow
   f. User clicks "Back"
   g. UI sends EvolutionResume
   h. Server continues evolution

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

## Related Documents

- `genetic-evolution.md` - Algorithm details (selection, mutation, fitness).
- `plant.md` - Tree organism and brain interface.
- `neural-net-brain-plan.md` - Neural network architecture.
