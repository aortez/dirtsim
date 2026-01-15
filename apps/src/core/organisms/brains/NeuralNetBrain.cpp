#include "NeuralNetBrain.h"

#include "Genome.h"
#include "WeightType.h"
#include "core/Assert.h"
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
//   - 6 (internal state: energy, water, age, stage, scale_factor, reserved)
//   - 7 (current action one-hot, all zeros if idle)
//   - 1 (action progress, 0.0 to 1.0)
constexpr int INPUT_SIZE = 2264;
constexpr int HIDDEN_SIZE = 48;

// Output layout:
//   - 7 (command logits: Wait, Cancel, GrowWood, GrowLeaf, GrowRoot, Reinforce, ProduceSeed)
//   - 225 (position logits)
constexpr int OUTPUT_SIZE = 232;

constexpr int W_IH_SIZE = INPUT_SIZE * HIDDEN_SIZE;
constexpr int B_H_SIZE = HIDDEN_SIZE;
constexpr int W_HO_SIZE = HIDDEN_SIZE * OUTPUT_SIZE;

constexpr int NUM_COMMANDS = 7;
constexpr int NUM_POSITIONS = 225;
constexpr int GRID_SIZE = 15;
constexpr int NUM_MATERIALS = 10;

double relu(double x)
{
    return std::max(0.0, x);
}

} // namespace

struct NeuralNetBrain::Impl {
    std::vector<WeightType> w_ih;
    std::vector<WeightType> b_h;
    std::vector<WeightType> w_ho;
    std::vector<WeightType> b_o;

    Impl()
        : w_ih(W_IH_SIZE, 0.0f), b_h(B_H_SIZE, 0.0f), w_ho(W_HO_SIZE, 0.0f), b_o(OUTPUT_SIZE, 0.0f)
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

    std::vector<double> forward(const std::vector<double>& input)
    {
        // Hidden layer: h = ReLU(W_ih @ input + b_h).
        std::vector<double> hidden(HIDDEN_SIZE);
        for (int h = 0; h < HIDDEN_SIZE; h++) {
            double sum = b_h[h];
            for (int i = 0; i < INPUT_SIZE; i++) {
                sum += input[i] * w_ih[i * HIDDEN_SIZE + h];
            }
            hidden[h] = relu(sum);
        }

        // Output layer: o = W_ho @ hidden + b_o (linear).
        std::vector<double> output(OUTPUT_SIZE);
        for (int o = 0; o < OUTPUT_SIZE; o++) {
            double sum = b_o[o];
            for (int h = 0; h < HIDDEN_SIZE; h++) {
                sum += hidden[h] * w_ho[h * OUTPUT_SIZE + o];
            }
            output[o] = sum;
        }

        return output;
    }

    std::vector<double> flattenSensoryData(const TreeSensoryData& sensory)
    {
        std::vector<double> input;
        input.reserve(INPUT_SIZE);

        // Flatten material histograms: [y][x][material].
        for (int y = 0; y < GRID_SIZE; y++) {
            for (int x = 0; x < GRID_SIZE; x++) {
                for (int m = 0; m < NUM_MATERIALS; m++) {
                    input.push_back(sensory.material_histograms[y][x][m]);
                }
            }
        }

        // Internal state (normalized to ~[0,1] range).
        input.push_back(sensory.total_energy / 200.0);
        input.push_back(sensory.total_water / 100.0);
        input.push_back(sensory.age_seconds / 100.0);
        input.push_back(static_cast<double>(sensory.stage) / 4.0);
        input.push_back(sensory.scale_factor / 10.0);
        input.push_back(0.0); // Reserved for future use.

        // Current action one-hot encoding (7 values).
        // Note: Wait and Cancel are never "in progress", so these will be 0.
        for (int i = 0; i < NUM_COMMANDS; i++) {
            if (sensory.current_action.has_value()
                && static_cast<int>(sensory.current_action.value()) == i) {
                input.push_back(1.0);
            }
            else {
                input.push_back(0.0);
            }
        }

        // Action progress (0.0 to 1.0).
        input.push_back(sensory.action_progress);

        return input;
    }

    TreeCommand interpretOutput(const std::vector<double>& output, const TreeSensoryData& sensory)
    {
        // First 7 outputs are command logits - use argmax.
        int command_idx = 0;
        double max_command = output[0];
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
        double max_pos = output[NUM_COMMANDS];
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
            case TreeCommandType::ReinforceCellCommand:
                return ReinforceCellCommand{ .position = world_pos };
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
    auto input = impl_->flattenSensoryData(sensory);
    auto output = impl_->forward(input);
    return impl_->interpretOutput(output, sensory);
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
