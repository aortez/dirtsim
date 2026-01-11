# NeuralNetBrain Implementation Plan

## Overview

Implement a neural network brain for tree organisms with factorized outputs (action type + position).

## Architecture

```
Input Layer (2706 neurons)
├── 15×15×10 = 2250 (material histograms)
├── 15×15×1 = 225 (light channel - future)
├── 15×15×1 = 225 (ownership channel - future)
└── 6 (internal state: energy, water, age, root_count, leaf_count, wood_count)

Hidden Layer (48 neurons)
└── ReLU activation

Output Layer (231 neurons)
├── 6 (action logits: WOOD, LEAF, ROOT, REINFORCE, SEED, WAIT)
└── 225 (position logits: 15×15 grid)
```

**Note**: Initial implementation uses only material histograms (2250) + internal state (6) = 2256 inputs. Light and ownership channels can be added later.

## Files to Create

```
src/core/organisms/brains/
├── NeuralNetBrain.h
├── NeuralNetBrain.cpp
└── Genome.h
```

## Implementation Steps

### Step 1: Genome Structure

Simple weight vector container with serialization.

```cpp
// Genome.h
struct Genome {
    std::vector<double> weights;

    static constexpr int INPUT_SIZE = 2256;   // 15×15×10 + 6 state
    static constexpr int HIDDEN_SIZE = 48;
    static constexpr int OUTPUT_SIZE = 231;   // 6 actions + 225 positions

    // Total weights: (2256×48) + 48 + (48×231) + 231 = 108,288 + 48 + 11,088 + 231 = 119,655
    static constexpr int TOTAL_WEIGHTS =
        (INPUT_SIZE * HIDDEN_SIZE) + HIDDEN_SIZE +  // W_ih + b_h
        (HIDDEN_SIZE * OUTPUT_SIZE) + OUTPUT_SIZE;  // W_ho + b_o

    static Genome random(std::mt19937& rng);
    static Genome fromFile(const std::string& path);
    void toFile(const std::string& path) const;
};
```

### Step 2: NeuralNetBrain Class

```cpp
// NeuralNetBrain.h
class NeuralNetBrain : public TreeBrain {
public:
    NeuralNetBrain();                           // Random weights
    explicit NeuralNetBrain(const Genome& g);   // From genome
    explicit NeuralNetBrain(uint32_t seed);     // Seeded random

    TreeCommand decide(const TreeSensoryData& sensory) override;

    Genome getGenome() const;
    void setGenome(const Genome& g);

private:
    static constexpr int INPUT_SIZE = Genome::INPUT_SIZE;
    static constexpr int HIDDEN_SIZE = Genome::HIDDEN_SIZE;
    static constexpr int OUTPUT_SIZE = Genome::OUTPUT_SIZE;

    // Network weights (stored flat for easy genome I/O)
    std::vector<double> w_ih_;    // INPUT × HIDDEN
    std::vector<double> b_h_;     // HIDDEN
    std::vector<double> w_ho_;    // HIDDEN × OUTPUT
    std::vector<double> b_o_;     // OUTPUT

    // Forward pass
    std::vector<double> forward(const std::vector<double>& input);

    // Helpers
    std::vector<double> flattenSensoryData(const TreeSensoryData& sensory);
    TreeCommand interpretOutput(const std::vector<double>& output,
                                const TreeSensoryData& sensory);

    static double relu(double x) { return std::max(0.0, x); }
};
```

### Step 3: Forward Pass Implementation

```cpp
std::vector<double> NeuralNetBrain::forward(const std::vector<double>& input) {
    // Hidden layer: h = ReLU(W_ih @ input + b_h)
    std::vector<double> hidden(HIDDEN_SIZE);
    for (int h = 0; h < HIDDEN_SIZE; h++) {
        double sum = b_h_[h];
        for (int i = 0; i < INPUT_SIZE; i++) {
            sum += input[i] * w_ih_[i * HIDDEN_SIZE + h];
        }
        hidden[h] = relu(sum);
    }

    // Output layer: o = W_ho @ hidden + b_o (linear)
    std::vector<double> output(OUTPUT_SIZE);
    for (int o = 0; o < OUTPUT_SIZE; o++) {
        double sum = b_o_[o];
        for (int h = 0; h < HIDDEN_SIZE; h++) {
            sum += hidden[h] * w_ho_[h * OUTPUT_SIZE + o];
        }
        output[o] = sum;
    }

    return output;
}
```

### Step 4: Output Interpretation

```cpp
enum class ActionType { WOOD = 0, LEAF, ROOT, REINFORCE, SEED, WAIT };

TreeCommand NeuralNetBrain::interpretOutput(
    const std::vector<double>& output,
    const TreeSensoryData& sensory)
{
    // First 6 outputs are action logits.
    int action_idx = 0;
    double max_action = output[0];
    for (int i = 1; i < 6; i++) {
        if (output[i] > max_action) {
            max_action = output[i];
            action_idx = i;
        }
    }

    ActionType action = static_cast<ActionType>(action_idx);

    if (action == ActionType::WAIT) {
        return WaitCommand{ .duration_seconds = 0.2 };
    }

    // Next 225 outputs are position logits.
    int pos_idx = 0;
    double max_pos = output[6];
    for (int i = 1; i < 225; i++) {
        if (output[6 + i] > max_pos) {
            max_pos = output[6 + i];
            pos_idx = i;
        }
    }

    int nx = pos_idx % 15;
    int ny = pos_idx / 15;

    // Map neural coords to world coords.
    Vector2i world_pos{
        sensory.world_offset.x + static_cast<int>(nx * sensory.scale_factor),
        sensory.world_offset.y + static_cast<int>(ny * sensory.scale_factor)
    };

    switch (action) {
        case ActionType::WOOD:
            return GrowWoodCommand{ .target_pos = world_pos };
        case ActionType::LEAF:
            return GrowLeafCommand{ .target_pos = world_pos };
        case ActionType::ROOT:
            return GrowRootCommand{ .target_pos = world_pos };
        case ActionType::REINFORCE:
            return ReinforceCellCommand{ .position = world_pos };
        case ActionType::SEED:
            return ProduceSeedCommand{ .position = world_pos };
        default:
            return WaitCommand{};
    }
}
```

### Step 5: Sensory Data Flattening

```cpp
std::vector<double> NeuralNetBrain::flattenSensoryData(const TreeSensoryData& sensory) {
    std::vector<double> input;
    input.reserve(INPUT_SIZE);

    // Flatten material histograms: [y][x][material] -> flat vector
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 15; x++) {
            for (int m = 0; m < 10; m++) {
                input.push_back(sensory.material_histograms[y][x][m]);
            }
        }
    }

    // Internal state (normalized to ~[0,1] range)
    input.push_back(sensory.total_energy / 200.0);    // Assume max ~200
    input.push_back(sensory.total_water / 100.0);     // Assume max ~100
    input.push_back(sensory.age_seconds / 100.0);     // Normalize age

    // Cell counts (need to compute from histograms or pass in)
    // For now, use placeholders - can enhance later
    input.push_back(0.0);  // root_count placeholder
    input.push_back(0.0);  // leaf_count placeholder
    input.push_back(0.0);  // wood_count placeholder

    return input;
}
```

### Step 6: Random Weight Initialization

Xavier initialization for stable gradients:

```cpp
Genome Genome::random(std::mt19937& rng) {
    Genome g;
    g.weights.resize(TOTAL_WEIGHTS);

    // Xavier init: stddev = sqrt(2 / (fan_in + fan_out))
    double ih_stddev = std::sqrt(2.0 / (INPUT_SIZE + HIDDEN_SIZE));
    double ho_stddev = std::sqrt(2.0 / (HIDDEN_SIZE + OUTPUT_SIZE));

    std::normal_distribution<double> ih_dist(0.0, ih_stddev);
    std::normal_distribution<double> ho_dist(0.0, ho_stddev);

    int idx = 0;

    // W_ih weights
    for (int i = 0; i < INPUT_SIZE * HIDDEN_SIZE; i++) {
        g.weights[idx++] = ih_dist(rng);
    }
    // b_h biases (zero init)
    for (int i = 0; i < HIDDEN_SIZE; i++) {
        g.weights[idx++] = 0.0;
    }
    // W_ho weights
    for (int i = 0; i < HIDDEN_SIZE * OUTPUT_SIZE; i++) {
        g.weights[idx++] = ho_dist(rng);
    }
    // b_o biases (zero init)
    for (int i = 0; i < OUTPUT_SIZE; i++) {
        g.weights[idx++] = 0.0;
    }

    return g;
}
```

### Step 7: Unit Tests

```cpp
// NeuralNetBrain_test.cpp

TEST(NeuralNetBrainTest, ForwardPassProducesCorrectOutputSize) {
    NeuralNetBrain brain(42);  // Seeded
    TreeSensoryData sensory{};
    // ... initialize sensory data

    TreeCommand cmd = brain.decide(sensory);
    // Should not crash, should return valid command
    EXPECT_TRUE(std::holds_alternative<WaitCommand>(cmd) ||
                std::holds_alternative<GrowWoodCommand>(cmd) ||
                ...);
}

TEST(NeuralNetBrainTest, DeterministicWithSameSeed) {
    NeuralNetBrain brain1(42);
    NeuralNetBrain brain2(42);

    TreeSensoryData sensory{};
    // ... same sensory data

    TreeCommand cmd1 = brain1.decide(sensory);
    TreeCommand cmd2 = brain2.decide(sensory);

    // Same seed + same input = same output
    EXPECT_EQ(cmd1.index(), cmd2.index());
}

TEST(NeuralNetBrainTest, GenomeRoundTrip) {
    NeuralNetBrain brain1(42);
    Genome g = brain1.getGenome();

    NeuralNetBrain brain2(g);
    EXPECT_EQ(brain1.getGenome().weights, brain2.getGenome().weights);
}
```

### Step 8: Integration with Scenario

Add option to use NN brain in TreeGerminationScenario:

```cpp
// Option A: Config flag
if (config.use_neural_brain) {
    tree->setBrain(std::make_unique<NeuralNetBrain>(config.brain_seed));
}

// Option B: Separate scenario
class NeuralTreeScenario : public Scenario { ... };
```

## Testing Plan

1. **Unit tests**: Forward pass shapes, determinism, genome round-trip
2. **Integration test**: NN brain in TreeGermination scenario - verify no crashes
3. **Visual test**: Run sim, watch tree flail randomly, verify commands are generated
4. **Comparison**: Side-by-side with RuleBasedBrain (NN should be worse initially)

## Future Enhancements

1. Add light channel (channel 10) to input when available
2. Add ownership channel (channel 11) to input
3. Add cell count inputs (root/leaf/wood counts)
4. Softmax sampling instead of argmax for exploration
5. Genome save/load to JSON or binary
6. Training harness with fitness evaluation

## Task Checklist

- [ ] Create Genome.h
- [ ] Create NeuralNetBrain.h
- [ ] Create NeuralNetBrain.cpp
- [ ] Add to CMakeLists.txt
- [ ] Unit tests for forward pass
- [ ] Unit tests for determinism
- [ ] Unit tests for genome round-trip
- [ ] Integration: hook up in scenario
- [ ] Visual debugging: run and observe
