# Tree Organism Design

## Overview

Trees are living organisms in the WorldB physics simulation that grow from seeds, consume resources, and interact with the material physics system. Trees are composed of cells (SEED, WOOD, LEAF, ROOT) that participate fully in physics simulation while being coordinated by a central "brain."

## Tree-Physics Integration

**Trees are made of physics cells with organism ownership:**
- Tree cells (SEED/WOOD/LEAF/ROOT) are regular CellB objects in the world grid
- Each has an `organism_id` metadata field marking which tree owns it
- **Physics acts on tree cells normally** (gravity, pressure, collisions, forces)
- Trees can be destroyed by physics (falling metal blocks, pressure, etc.)
- Trees rely on material properties (cohesion, adhesion) for structural integrity
- TreeManager tracks which cells belong to which tree via `cell_to_tree` map

## Architecture

### File Structure

```
src/core/organisms/
├── TreeTypes.h              // TreeId, TreeCommand variants, TreeSensoryData
├── Tree.h/cpp               // Tree class
├── TreeCommandProcessor.h/cpp // Command validation and execution (injectable)
├── TreeManager.h/cpp        // Manages all trees, lifecycle
├── TreeBrain.h              // Abstract brain interface
└── brains/
    ├── RuleBasedBrain.h/cpp    // Hand-coded behavior (Phase 2)
    ├── NeuralNetBrain.h/cpp    // Neural network (✅ IMPLEMENTED)
    └── LLMBrain.h/cpp          // Ollama integration (future)
```

### Command Processing (✅ IMPLEMENTED)

Tree commands are validated and executed by an injectable `ITreeCommandProcessor`:

```cpp
class ITreeCommandProcessor {
public:
    virtual ~ITreeCommandProcessor() = default;
    virtual CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) = 0;
};
```

Trees hold a `std::unique_ptr<ITreeCommandProcessor> processor` which can be swapped for testing.
This enables recording/mock processors to verify brain behavior without running full simulation.

### Tree Commands

Trees execute commands that take time to complete. Energy costs are determined by TreeCommandProcessor.

```cpp
// TreeCommands.h

struct WaitCommand {};    // Do nothing this tick.
struct CancelCommand {};  // Cancel in-progress action.

// Action commands have execution time and target position.
struct GrowWoodCommand { Vector2i target_pos; double execution_time_seconds = 3.0; };
struct GrowLeafCommand { Vector2i target_pos; double execution_time_seconds = 0.5; };
struct GrowRootCommand { Vector2i target_pos; double execution_time_seconds = 2.0; };
struct ReinforceCellCommand { Vector2i position; double execution_time_seconds = 0.5; };
struct ProduceSeedCommand { Vector2i position; double execution_time_seconds = 2.0; };

using TreeCommand = std::variant<
    WaitCommand,
    CancelCommand,
    GrowWoodCommand,
    GrowLeafCommand,
    GrowRootCommand,
    ReinforceCellCommand,
    ProduceSeedCommand
>;
```

### Tree Class

```cpp
enum class GrowthStage {
    SEED,
    GERMINATION,
    SAPLING,
    MATURE,
    DECLINE
};

class Tree {
public:
    TreeId getId() const { return id_; }
    uint32_t getAge() const { return age_; }

    void update(WorldB& world, double deltaTime);

    // Resources (aggregated from cells)
    double getTotalEnergy() const { return total_energy_; }
    double getTotalWater() const { return total_water_; }

    // Cell tracking
    const std::unordered_set<Vector2i>& getCells() const { return cells_; }
    void addCell(Vector2i pos);
    void removeCell(Vector2i pos);

private:
    TreeId id_;
    std::unique_ptr<TreeBrain> brain_;

    // Cells owned by this tree (positions in world grid)
    std::unordered_set<Vector2i> cells_;

    // Organism state
    uint32_t age_ = 0;
    GrowthStage stage_ = GrowthStage::SEED;

    // Aggregated resources (computed from world cells)
    double total_energy_ = 0;
    double total_water_ = 0;

    // Command execution
    std::optional<TreeCommand> current_command_;
    uint32_t steps_remaining_ = 0;

    void executeCommand(WorldB& world);
    void decideNextAction(const WorldB& world);
    void updateResources(const WorldB& world);
};
```

### Tree Brain Interface

Trees have pluggable brains that make growth decisions. Brain state/memory lives in brain implementations.

```cpp
// TreeBrain.h

struct TreeSensoryData {
    // Fixed-size neural grid (scale-invariant).
    static constexpr int GRID_SIZE = 15;

    // Channel indices for sensory_channels array.
    static constexpr int NUM_MATERIALS = 10;      // AIR, DIRT, LEAF, METAL, ROOT, SAND, SEED, WALL, WATER, WOOD
    static constexpr int LIGHT_CHANNEL = 10;      // Average brightness (0.0-1.0).
    static constexpr int OWNERSHIP_CHANNEL = 11;  // Fraction of cells belonging to this tree (0.0-1.0).
    static constexpr int NUM_CHANNELS = 12;

    // Multi-channel sensory grid: [y][x][channel]
    // Channels 0-9:  Material distribution (probabilities sum to 1.0).
    // Channel 10:    Light level (average brightness in region).
    // Channel 11:    Ownership (fraction of cells belonging to this tree).
    std::array<std::array<std::array<double, NUM_CHANNELS>, GRID_SIZE>, GRID_SIZE>
        sensory_channels;

    // Metadata about mapping.
    int actual_width;       // Real bounding box size.
    int actual_height;
    double scale_factor;    // Real cells per neural cell.
    Vector2i world_offset;  // Top-left corner in world coords.

    // Internal state.
    uint32_t age;
    GrowthStage stage;
    double total_energy;
    double total_water;
    int root_count;
    int leaf_count;
    int wood_count;
};

class TreeBrain {
public:
    virtual ~TreeBrain() = default;
    virtual TreeCommand decide(const TreeSensoryData& sensory) = 0;
};
```

### Scale-Invariant Sensory System (✅ IMPLEMENTED)

Trees use a fixed 15×15 neural grid regardless of actual tree size:

**Small trees (≤15×15 cells)**:
- Fixed 15×15 world-cell viewing window centered on seed position
- 1:1 mapping (each neural cell = one world cell)
- Material channels are one-hot: [0,0,0,1,0,0,0,0,0,0] for pure materials
- Viewing window follows seed as it moves (physics-aware)

**Large trees (>15×15 cells)**:
- Bounding box + 1-cell padding, downsampled to 15×15
- scale_factor > 1.0, each neural cell aggregates multiple world cells
- Material channels show distributions: [0.4 WOOD, 0.3 LEAF, 0.2 AIR, 0.1 DIRT, ...]

**Sensory Channels**:
- Channels 0-9: Material distribution (normalized probabilities)
- Channel 10 (LIGHT): Average brightness from cell colors (0.0-1.0)
- Channel 11 (OWNERSHIP): Fraction of cells in region belonging to this tree

**Implementation Notes**:
- Seed position tracked via Tree.seed_position (updated on transfers)
- Sensory channels populated by sampling world grid
- Light values come from cell.getColor() via ColorNames::brightness()
- Ownership determined by checking organism_id against tree id
- Visualization in UI via NeuralGridRenderer (50/50 split with world view)

```cpp
TreeSensoryData Tree::gatherSensoryData(const World& world) {
    TreeSensoryData data;

    // Calculate bounding box + 1 cell padding.
    auto bbox = calculateBoundingBox();
    bbox.expand(1);

    data.actual_width = bbox.width;
    data.actual_height = bbox.height;
    data.world_offset = bbox.top_left;
    data.scale_factor = std::max(
        (double)bbox.width / TreeSensoryData::GRID_SIZE,
        (double)bbox.height / TreeSensoryData::GRID_SIZE
    );

    // Compute channels for each neural grid cell.
    for (int ny = 0; ny < 15; ny++) {
        for (int nx = 0; nx < 15; nx++) {
            // Map neural coords to world region.
            int wx_start = bbox.x + (int)(nx * data.scale_factor);
            int wy_start = bbox.y + (int)(ny * data.scale_factor);
            int wx_end = bbox.x + (int)((nx + 1) * data.scale_factor);
            int wy_end = bbox.y + (int)((ny + 1) * data.scale_factor);

            // Sample cells in region.
            std::array<int, NUM_MATERIALS> counts = {};
            double total_brightness = 0.0;
            int owned_cells = 0;
            int total_cells = 0;

            for (int wy = wy_start; wy < wy_end; wy++) {
                for (int wx = wx_start; wx < wx_end; wx++) {
                    const Cell& cell = world.getData().at(wx, wy);

                    // Material counting.
                    counts[static_cast<int>(cell.material_type)]++;

                    // Light accumulation.
                    total_brightness += ColorNames::brightness(cell.getColor());

                    // Ownership check.
                    if (world.getOrganismManager().at({wx, wy}) == id_) {
                        owned_cells++;
                    }

                    total_cells++;
                }
            }

            // Normalize and store channels.
            if (total_cells > 0) {
                for (int i = 0; i < NUM_MATERIALS; i++) {
                    data.sensory_channels[ny][nx][i] = counts[i] / (double)total_cells;
                }
                data.sensory_channels[ny][nx][LIGHT_CHANNEL] = total_brightness / total_cells;
                data.sensory_channels[ny][nx][OWNERSHIP_CHANNEL] = owned_cells / (double)total_cells;
            }
        }
    }

    return data;
}
```

### TreeManager

```cpp
class TreeManager {
public:
    void update(WorldB& world, double deltaTime);
    TreeId plantSeed(WorldB& world, uint32_t x, uint32_t y);
    void removeTree(TreeId id);
    void notifyTransfers(const std::vector<OrganismTransfer>& transfers);  // ✅ IMPLEMENTED

    const std::unordered_map<TreeId, Tree>& getTrees() const { return trees_; }

private:
    std::unordered_map<TreeId, Tree> trees_;
    std::unordered_map<Vector2i, TreeId> cell_to_tree_;
    uint32_t next_tree_id_ = 1;
};
```

**Organism Tracking (✅ IMPLEMENTED)**:
- Physics transfers automatically preserve organism_id (Cell.cpp:198-206)
- World collects OrganismTransfer events during applyTransfers()
- TreeManager::notifyTransfers() batch-updates tracking in O(transfers)
- Tree.cells and cell_to_tree_ map stay synchronized with physics
- seed_position updated when seed cell moves

### Update Flow

```cpp
void Tree::update(WorldB& world, double deltaTime) {
    age_++;

    // Execute current command (tick down timer)
    if (current_command_.has_value()) {
        if (--steps_remaining_ == 0) {
            executeCommand(world);
            current_command_.reset();
        }
    }

    // When idle, ask brain for next action
    if (!current_command_.has_value()) {
        decideNextAction(world);
    }

    // Resource updates (continuous)
    updateResources(world);
}
```

## Growth Mechanics

### Atomic Replacement

Growth replaces target cell atomically to prevent cascading physics effects:

```cpp
void Tree::executeCommand(WorldB& world) {
    std::visit([&](auto&& cmd) {
        using T = std::decay_t<decltype(cmd)>;

        if constexpr (std::is_same_v<T, GrowWoodCommand> ||
                      std::is_same_v<T, GrowLeafCommand> ||
                      std::is_same_v<T, GrowRootCommand>) {

            Cell& target = world.getCell(cmd.target_pos);

            // TODO: Generate dynamic pressure from displaced material
            // This would push adjacent materials outward realistically

            // Simple replacement (material disappears)
            MaterialType new_type = /* extract from command type */;
            target.setMaterialType(new_type);
            target.setFillRatio(1.0);
            target.setOrganismId(id_);

            cells_.insert(cmd.target_pos);
            total_energy_ -= cmd.energy_cost;
        }
    }, *current_command_);
}
```

### Growth Constraints (✅ IMPLEMENTED in TreeCommandProcessor)

All growth commands are validated before execution:

**WOOD growth:**
- Must be cardinally adjacent to WOOD or SEED belonging to this tree

**LEAF growth:**
- Must be cardinally adjacent to WOOD belonging to this tree

**ROOT growth:**
- Must be cardinally adjacent to SEED or ROOT belonging to this tree

**SEED production:**
- Must be cardinally adjacent to WOOD or LEAF belonging to this tree
- Target cell must be AIR (seeds need space to fall)
- Target cell cannot be owned by another organism

**All commands:**
- Target must be within world bounds
- Must have sufficient energy

## Resource Systems

**Air**
- Absorbed by leaves and at a lower rate, by roots in dirt or air.
- Required for photosynthesis.

**Light**
- Cast from top of world downward
- Blocked by opaque materials
- LEAF cells collect light based on exposure
- Drives photosynthesis

**Water**
- Absorbed from adjacent WATER cells (very high rate)
- Can extract from DIRT (low rate)
- Can extract from AIR (very low rate)
- Required for growth and photosynthesis

**Nutrients**
- Stored in DIRT cells as metadata [0.0-1.0]
- ROOT cells extract nutrients
- Regenerate slowly over time

**Energy**
- Internal tree resource
- Produced via photosynthesis
- Consumed by growth and maintenance
- Tracked per tree, distributed across cells

### Photosynthesis (Phase 3)
```
Energy Production =
    (Light × Light_Efficiency × LEAF_Count) +
    (Water × Water_Efficiency) +
    (Nutrients × Nutrient_Efficiency) -
    (Maintenance_Cost × Total_Cells)
```

### Growth Costs
```
Base Costs:
- WOOD: 10 energy, 50 timesteps
- LEAF: 8 energy, 30 timesteps
- ROOT: 12 energy, 60 timesteps

Material displacement multipliers (future):
- AIR: ×1.0
- WATER: ×1.5
- DIRT: ×2.0
```

## Tree Lifecycle Stages

**SEED** (0-100 timesteps)
- Single cell, absorbs water
- Waits for germination conditions

**GERMINATION** (100-200 timesteps)
- SEED → WOOD conversion
- Grows first ROOT downward

**SAPLING** (200-1000 timesteps)
- Rapid growth
- Prioritizes ROOTs and LEAFs

**MATURE** (1000+ timesteps)
- Balanced growth
- Can produce SEEDs
- Competes with other trees

**DECLINE** (variable)
- Resource shortage triggers
- Cells die (convert to DIRT)
- Can recover if conditions improve

## Growth Patterns (Brain Implementations)

### Priority System (Example for RuleBasedBrain)
1. Survival: ROOT cells if nutrients < 20%
2. Energy: LEAF cells if energy production negative
3. Structure: WOOD cells for height/spread
4. Reproduction: SEED production if surplus energy

### Directional Preferences
- LEAF: Grows toward light and away from wood
- ROOT: Grows toward dirt and away from wood
- WOOD: Grows upward and away from other wood

## Implementation Plan

### Phase 1: Foundation + Neural Grid Visualization
**Goal**: SEED material, tree organisms, and visual debugging of tree perception

**Status: DONE**

### Phase 2: Growth System
**Goal**: Intelligent germination and balanced growth with resource constraints

**Status: ✅ COMPLETE**

- Contact-based germination (observe dirt → ROOT → WOOD)
- TreeCommandProcessor validates energy, adjacency, bounds
- RuleBasedBrain implements trunk/branch/leaf growth patterns
- Balanced growth with left/right mass tracking
- 13 passing tests

### Phase 3: Resource Economy

**Goal**: Trees produce and consume energy based on light exposure.

**Status: IN PROGRESS**

#### Light System Integration

Light is calculated by WorldLightCalculator **before** organism updates, so trees can query current light values:

```
World::advanceTime() order:
1. light_calculator_.calculate()   ← Light computed first
2. organism_manager_->update()     ← Tree::update() runs here (can read light)
3. resolveForces()
4. ...
```

Trees access light via `cell.getColor()` and `ColorNames::brightness()`.

#### TreePhysiology Helper Functions

New file: `src/core/organisms/TreePhysiology.h/cpp`

Pure functions for testability:

```cpp
namespace TreePhysiology {

struct PhotosynthesisResult {
    double energy_produced;
    double water_consumed;
};

// Compute energy production from light exposure.
// leaf_count: Number of LEAF cells.
// average_light: Average brightness across LEAF cells (0.0-1.0).
// available_water: Current water reserves.
// deltaTime: Time step in seconds.
PhotosynthesisResult computePhotosynthesis(
    int leaf_count,
    double average_light,
    double available_water,
    double deltaTime);

// Compute energy drain from maintaining tree structure.
// total_cells: Total number of cells in tree.
// deltaTime: Time step in seconds.
double computeMaintenanceCost(
    int total_cells,
    double deltaTime);

} // namespace TreePhysiology
```

#### Energy Flow

```
Photosynthesis:
  energy += LEAF_count × average_light × PHOTOSYNTHESIS_RATE × deltaTime

Maintenance:
  energy -= total_cells × MAINTENANCE_COST × deltaTime

Net energy determines growth capacity and survival.
```

#### Implementation Steps
1. ✅ Light system integrated into World (WorldLightCalculator)
2. Add LIGHT_CHANNEL and OWNERSHIP_CHANNEL to TreeSensoryData
3. Implement TreePhysiology helper functions with unit tests
4. Call TreePhysiology from Tree::updateResources()
5. Test: Trees in light grow, trees in shadow decline

### Phase 4: Advanced Features
1. Multiple tree competition
2. Seed production and dispersal
3. Tree death/decomposition
4. Visual differentiation
5. Performance optimization

### Phase 5: AI Brains

#### Neural Network Brain (✅ IMPLEMENTED)

**Status**: Basic implementation complete. NeuralNetBrain class with random weights, selectable via TreeGerminationConfig.brain_type.

**Architecture** (factorized outputs):
```
Input Layer (2256 neurons)
├── 15×15×10 = 2250 (material histograms)
└── 6 (internal state: energy, water, age, stage, scale_factor, reserved)

Hidden Layer (48 neurons, ReLU activation)

Output Layer (231 neurons, linear)
├── 6 (action logits: WOOD, LEAF, ROOT, REINFORCE, SEED, WAIT)
└── 225 (position logits: 15×15 grid)
```

**Key Design Decisions**:
- Factorized outputs: Action type and position selected independently via argmax.
- This allows adding new action types cheaply (just add neurons to action head).
- ~120K parameters total, suitable for genetic algorithm evolution.
- Xavier weight initialization for stable gradients.

**Files**:
- `src/core/organisms/brains/NeuralNetBrain.h/cpp` - Brain implementation.
- `src/core/organisms/brains/Genome.h/cpp` - Weight vector with serialization.

**Tests**:
- `src/core/organisms/tests/NeuralNetBrain_test.cpp` - Unit tests for brain in isolation.
- `src/core/organisms/tests/TreeNeuralNetwork_test.cpp` - Integration test with Tree and World.
  Uses `RecordingCommandProcessor` to verify commands are produced.

**Configuration**:
TreeGerminationConfig has `brain_type` (RULE_BASED or NEURAL_NET) and `neural_seed`.

**Next Steps**:
- Add light channel (channel 10) to input.
- Add ownership channel (channel 11) to input.
- Training harness for genetic algorithm evolution.
- Genome save/load to file.
- UI selector for brain type.

#### Genetic Algorithm Evolution

See `genetic-evolution.md` for algorithm details and `evolution-framework.md` for system architecture.

**Benefits of channel-based input**:
- Fixed network size regardless of tree size.
- Network learns to interpret "blur" (distributions vs one-hot).
- Small trees get crisp signals, large trees get aggregated view.
- Smooth transition as tree grows, no discontinuities.

#### LLM Brain (Future)

Uses Ollama for high-level strategic decision making (see design_docs/ai-integration-ideas.md).

#### Brain Comparison Tools (Future)

- Visualize evolved network weights.
- Compare fitness across different architectures.
- Export successful strategies for analysis.

## Integration with World

```cpp
class World {
    std::unique_ptr<TreeManager> tree_manager_;

    // In constructor:
    tree_manager_ = std::make_unique<TreeManager>();

    // In advanceTime():
    if (tree_manager_) {
        tree_manager_->update(*this, scaledDeltaTime);
    }

    TreeManager* getTreeManager() { return tree_manager_.get(); }
};
```

## Testing Strategy

### Unit Tests
- TreeManager operations
- Resource calculations
- Command execution
- Cell ownership tracking

### Visual Tests
- Single tree growth
- Multiple tree competition
- Resource depletion
- Physics destruction (falling blocks crushing trees)
