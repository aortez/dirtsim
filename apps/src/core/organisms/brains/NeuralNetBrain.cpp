#include "NeuralNetBrain.h"

#include "Genome.h"
#include "WeightType.h"
#include "core/Assert.h"
#include "core/ScopeTimer.h"
#include "core/organisms/TreeCommands.h"
#include "core/organisms/TreeSensoryData.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace DirtSim {

namespace {

// Input layout:
//   - 15×15×10 = 2250 (material histograms)
//   - 15×15 = 225 (light levels)
//   - 6 (internal state: energy, water, age, stage, scale_factor, reserved)
//   - 6 (current action one-hot, all zeros if idle)
//   - 1 (action progress, 0.0 to 1.0)
constexpr int INPUT_SIZE = 2488;
constexpr int HIDDEN_SIZE = 48;

// Output layout:
//   - 6 (command logits: Wait, Cancel, GrowWood, GrowLeaf, GrowRoot, ProduceSeed)
//   - 225 (position logits)
constexpr int OUTPUT_SIZE = 231;

constexpr int W_IH_SIZE = INPUT_SIZE * HIDDEN_SIZE;
constexpr int B_H_SIZE = HIDDEN_SIZE;
constexpr int W_HO_SIZE = HIDDEN_SIZE * OUTPUT_SIZE;

constexpr int NUM_COMMANDS = 6;
constexpr int NUM_POSITIONS = 225;
constexpr int GRID_SIZE = 15;
constexpr int NUM_MATERIALS = 10;

WeightType relu(WeightType x)
{
    return std::max(static_cast<WeightType>(0.0f), x);
}

} // namespace

struct NeuralNetBrain::Impl {
    std::vector<WeightType> w_ih;
    std::vector<WeightType> b_h;
    std::vector<WeightType> w_ho;
    std::vector<WeightType> b_o;
    std::vector<WeightType> input_buffer;
    std::vector<WeightType> hidden_buffer;
    std::vector<WeightType> output_buffer;

    Impl()
        : w_ih(W_IH_SIZE, 0.0f),
          b_h(B_H_SIZE, 0.0f),
          w_ho(W_HO_SIZE, 0.0f),
          b_o(OUTPUT_SIZE, 0.0f),
          input_buffer(INPUT_SIZE, 0.0f),
          hidden_buffer(HIDDEN_SIZE, 0.0f),
          output_buffer(OUTPUT_SIZE, 0.0f)
    {}

    void loadFromGenome(const Genome& g)
    {
        int idx = 0;

        for (int i = 0; i < W_IH_SIZE; i++) {
            w_ih[i] = g.weights[idx++];
        }
        for (int i = 0; i < B_H_SIZE; i++) {
            b_h[i] = g.weights[idx++];
        }
        for (int i = 0; i < W_HO_SIZE; i++) {
            w_ho[i] = g.weights[idx++];
        }
        for (int i = 0; i < OUTPUT_SIZE; i++) {
            b_o[i] = g.weights[idx++];
        }
    }

    Genome toGenome() const
    {
        Genome g;
        int idx = 0;

        for (int i = 0; i < W_IH_SIZE; i++) {
            g.weights[idx++] = w_ih[i];
        }
        for (int i = 0; i < B_H_SIZE; i++) {
            g.weights[idx++] = b_h[i];
        }
        for (int i = 0; i < W_HO_SIZE; i++) {
            g.weights[idx++] = w_ho[i];
        }
        for (int i = 0; i < OUTPUT_SIZE; i++) {
            g.weights[idx++] = b_o[i];
        }

        return g;
    }

    const std::vector<WeightType>& forward(const std::vector<WeightType>& input)
    {
        std::copy(b_h.begin(), b_h.end(), hidden_buffer.begin());
        for (int i = 0; i < INPUT_SIZE; i++) {
            const WeightType input_value = input[i];
            if (input_value == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_ih[i * HIDDEN_SIZE];
            for (int h = 0; h < HIDDEN_SIZE; h++) {
                hidden_buffer[h] += input_value * weights[h];
            }
        }
        for (int h = 0; h < HIDDEN_SIZE; h++) {
            hidden_buffer[h] = relu(hidden_buffer[h]);
        }

        // Output layer: o = W_ho @ hidden + b_o (linear).
        std::copy(b_o.begin(), b_o.end(), output_buffer.begin());
        for (int h = 0; h < HIDDEN_SIZE; h++) {
            const WeightType hidden_value = hidden_buffer[h];
            const WeightType* weights = &w_ho[h * OUTPUT_SIZE];
            for (int o = 0; o < OUTPUT_SIZE; o++) {
                output_buffer[o] += hidden_value * weights[o];
            }
        }

        return output_buffer;
    }

    const std::vector<WeightType>& flattenSensoryData(const TreeSensoryData& sensory)
    {
        int index = 0;

        // Flatten material histograms: [y][x][material].
        for (int y = 0; y < GRID_SIZE; y++) {
            for (int x = 0; x < GRID_SIZE; x++) {
                for (int m = 0; m < NUM_MATERIALS; m++) {
                    input_buffer[index++] =
                        static_cast<WeightType>(sensory.material_histograms[y][x][m]);
                }
            }
        }

        // Flatten light levels: [y][x].
        for (int y = 0; y < GRID_SIZE; y++) {
            for (int x = 0; x < GRID_SIZE; x++) {
                input_buffer[index++] = static_cast<WeightType>(sensory.light_levels[y][x]);
            }
        }

        // Internal state (normalized to ~[0,1] range).
        input_buffer[index++] = static_cast<WeightType>(sensory.total_energy / 200.0);
        input_buffer[index++] = static_cast<WeightType>(sensory.total_water / 100.0);
        input_buffer[index++] = static_cast<WeightType>(sensory.age_seconds / 100.0);
        input_buffer[index++] = static_cast<WeightType>(static_cast<double>(sensory.stage) / 4.0);
        input_buffer[index++] = static_cast<WeightType>(sensory.scale_factor / 10.0);
        input_buffer[index++] = static_cast<WeightType>(0.0f); // Reserved for future use.

        // Current action one-hot encoding (6 values).
        // Note: Wait and Cancel are never "in progress", so these will be 0.
        for (int i = 0; i < NUM_COMMANDS; i++) {
            if (sensory.current_action.has_value()
                && static_cast<int>(sensory.current_action.value()) == i) {
                input_buffer[index++] = static_cast<WeightType>(1.0f);
            }
            else {
                input_buffer[index++] = static_cast<WeightType>(0.0f);
            }
        }

        // Action progress (0.0 to 1.0).
        input_buffer[index++] = static_cast<WeightType>(sensory.action_progress);

        return input_buffer;
    }

    TreeCommand interpretOutput(
        const std::vector<WeightType>& output, const TreeSensoryData& sensory)
    {
        // First 6 outputs are command logits - use argmax.
        int command_idx = 0;
        WeightType max_command = output[0];
        for (int i = 1; i < NUM_COMMANDS; i++) {
            if (output[i] > max_command) {
                max_command = output[i];
                command_idx = i;
            }
        }

        auto command_type = static_cast<TreeCommandType>(command_idx);

        // Handle instant commands (no position needed).
        if (command_type == TreeCommandType::WaitCommand) {
            return WaitCommand{};
        }
        if (command_type == TreeCommandType::CancelCommand) {
            return CancelCommand{};
        }

        // Action commands need position - extract from next 225 outputs.
        int pos_idx = 0;
        WeightType max_pos = output[NUM_COMMANDS];
        for (int i = 1; i < NUM_POSITIONS; i++) {
            if (output[NUM_COMMANDS + i] > max_pos) {
                max_pos = output[NUM_COMMANDS + i];
                pos_idx = i;
            }
        }

        int nx = pos_idx % GRID_SIZE;
        int ny = pos_idx / GRID_SIZE;

        // Map neural coords to world coords.
        Vector2i world_pos{ sensory.world_offset.x + static_cast<int>(nx * sensory.scale_factor),
                            sensory.world_offset.y + static_cast<int>(ny * sensory.scale_factor) };

        // Build action command based on type.
        switch (command_type) {
            case TreeCommandType::WaitCommand:
                return WaitCommand{};
            case TreeCommandType::CancelCommand:
                return CancelCommand{};
            case TreeCommandType::GrowWoodCommand:
                return GrowWoodCommand{ .target_pos = world_pos };
            case TreeCommandType::GrowLeafCommand:
                return GrowLeafCommand{ .target_pos = world_pos };
            case TreeCommandType::GrowRootCommand:
                return GrowRootCommand{ .target_pos = world_pos };
            case TreeCommandType::ProduceSeedCommand:
                return ProduceSeedCommand{ .position = world_pos };
        }

        DIRTSIM_ASSERT(false, "Unreachable: all TreeCommandType enum values handled");
        return WaitCommand{};
    }
};

NeuralNetBrain::NeuralNetBrain() : impl_(std::make_unique<Impl>())
{
    std::mt19937 rng(std::random_device{}());
    auto genome = Genome::random(rng);
    impl_->loadFromGenome(genome);
}

NeuralNetBrain::NeuralNetBrain(const Genome& genome) : impl_(std::make_unique<Impl>())
{
    impl_->loadFromGenome(genome);
}

NeuralNetBrain::NeuralNetBrain(uint32_t seed) : impl_(std::make_unique<Impl>())
{
    std::mt19937 rng(seed);
    auto genome = Genome::random(rng);
    impl_->loadFromGenome(genome);
}

NeuralNetBrain::~NeuralNetBrain() = default;

TreeCommand NeuralNetBrain::decide(const TreeSensoryData& sensory)
{
    const auto& input = impl_->flattenSensoryData(sensory);
    const auto& output = impl_->forward(input);
    return impl_->interpretOutput(output, sensory);
}

TreeCommand NeuralNetBrain::decideWithTimers(const TreeSensoryData& sensory, Timers& timers)
{
    const std::vector<WeightType>* input = nullptr;
    {
        ScopeTimer timer(timers, "tree_brain_flatten");
        input = &impl_->flattenSensoryData(sensory);
    }

    const std::vector<WeightType>* output = nullptr;
    {
        ScopeTimer timer(timers, "tree_brain_forward");
        output = &impl_->forward(*input);
    }

    return impl_->interpretOutput(*output, sensory);
}

Genome NeuralNetBrain::getGenome() const
{
    return impl_->toGenome();
}

void NeuralNetBrain::setGenome(const Genome& genome)
{
    impl_->loadFromGenome(genome);
}

} // namespace DirtSim
