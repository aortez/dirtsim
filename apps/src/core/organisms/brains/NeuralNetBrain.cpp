#include "NeuralNetBrain.h"

#include "Genome.h"
#include "core/organisms/TreeCommands.h"
#include "core/organisms/TreeSensoryData.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace DirtSim {

namespace {

constexpr int INPUT_SIZE = 2256;
constexpr int HIDDEN_SIZE = 48;
constexpr int OUTPUT_SIZE = 231;

constexpr int W_IH_SIZE = INPUT_SIZE * HIDDEN_SIZE;
constexpr int B_H_SIZE = HIDDEN_SIZE;
constexpr int W_HO_SIZE = HIDDEN_SIZE * OUTPUT_SIZE;

constexpr int NUM_ACTIONS = 6;
constexpr int NUM_POSITIONS = 225;
constexpr int GRID_SIZE = 15;
constexpr int NUM_MATERIALS = 10;

enum class ActionType { WOOD = 0, LEAF, ROOT, REINFORCE, SEED, WAIT };

double relu(double x)
{
    return std::max(0.0, x);
}

} // namespace

struct NeuralNetBrain::Impl {
    std::vector<double> w_ih;
    std::vector<double> b_h;
    std::vector<double> w_ho;
    std::vector<double> b_o;

    Impl() : w_ih(W_IH_SIZE, 0.0), b_h(B_H_SIZE, 0.0), w_ho(W_HO_SIZE, 0.0), b_o(OUTPUT_SIZE, 0.0)
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

        return input;
    }

    TreeCommand interpretOutput(const std::vector<double>& output, const TreeSensoryData& sensory)
    {
        // First 6 outputs are action logits.
        int action_idx = 0;
        double max_action = output[0];
        for (int i = 1; i < NUM_ACTIONS; i++) {
            if (output[i] > max_action) {
                max_action = output[i];
                action_idx = i;
            }
        }

        auto action = static_cast<ActionType>(action_idx);

        if (action == ActionType::WAIT) {
            return WaitCommand{ .duration_seconds = 0.2 };
        }

        // Next 225 outputs are position logits.
        int pos_idx = 0;
        double max_pos = output[NUM_ACTIONS];
        for (int i = 1; i < NUM_POSITIONS; i++) {
            if (output[NUM_ACTIONS + i] > max_pos) {
                max_pos = output[NUM_ACTIONS + i];
                pos_idx = i;
            }
        }

        int nx = pos_idx % GRID_SIZE;
        int ny = pos_idx / GRID_SIZE;

        // Map neural coords to world coords.
        Vector2i world_pos{ sensory.world_offset.x + static_cast<int>(nx * sensory.scale_factor),
                            sensory.world_offset.y + static_cast<int>(ny * sensory.scale_factor) };

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
            case ActionType::WAIT:
                return WaitCommand{ .duration_seconds = 0.2 };
        }

        return WaitCommand{ .duration_seconds = 0.2 };
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
