#include "DuckNeuralNetBrain.h"

#include "WeightType.h"
#include "core/Assert.h"
#include "core/organisms/Duck.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace DirtSim {

namespace {

constexpr int GRID_SIZE = DuckSensoryData::GRID_SIZE;
constexpr int NUM_MATERIALS = DuckSensoryData::NUM_MATERIALS;

constexpr int INPUT_HISTOGRAM_SIZE = GRID_SIZE * GRID_SIZE * NUM_MATERIALS;
constexpr int INPUT_SIZE = INPUT_HISTOGRAM_SIZE + 4;
constexpr int HIDDEN_SIZE = 32;
constexpr int OUTPUT_SIZE = 2;

constexpr int W_IH_SIZE = INPUT_SIZE * HIDDEN_SIZE;
constexpr int B_H_SIZE = HIDDEN_SIZE;
constexpr int W_HO_SIZE = HIDDEN_SIZE * OUTPUT_SIZE;
constexpr int B_O_SIZE = OUTPUT_SIZE;
constexpr int TOTAL_WEIGHTS = W_IH_SIZE + B_H_SIZE + W_HO_SIZE + B_O_SIZE;

WeightType relu(WeightType x)
{
    return std::max(static_cast<WeightType>(0.0f), x);
}

} // namespace

struct DuckNeuralNetBrain::Impl {
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
          b_o(B_O_SIZE, 0.0f),
          input_buffer(INPUT_SIZE, 0.0f),
          hidden_buffer(HIDDEN_SIZE, 0.0f),
          output_buffer(OUTPUT_SIZE, 0.0f)
    {}

    void loadFromGenome(const Genome& genome)
    {
        DIRTSIM_ASSERT(
            genome.weights.size() == TOTAL_WEIGHTS,
            "DuckNeuralNetBrain: Genome weight count mismatch");

        int idx = 0;
        for (int i = 0; i < W_IH_SIZE; ++i) {
            w_ih[i] = genome.weights[idx++];
        }
        for (int i = 0; i < B_H_SIZE; ++i) {
            b_h[i] = genome.weights[idx++];
        }
        for (int i = 0; i < W_HO_SIZE; ++i) {
            w_ho[i] = genome.weights[idx++];
        }
        for (int i = 0; i < B_O_SIZE; ++i) {
            b_o[i] = genome.weights[idx++];
        }
    }

    Genome toGenome() const
    {
        Genome genome(static_cast<size_t>(TOTAL_WEIGHTS));
        int idx = 0;

        for (int i = 0; i < W_IH_SIZE; ++i) {
            genome.weights[idx++] = w_ih[i];
        }
        for (int i = 0; i < B_H_SIZE; ++i) {
            genome.weights[idx++] = b_h[i];
        }
        for (int i = 0; i < W_HO_SIZE; ++i) {
            genome.weights[idx++] = w_ho[i];
        }
        for (int i = 0; i < B_O_SIZE; ++i) {
            genome.weights[idx++] = b_o[i];
        }

        return genome;
    }

    const std::vector<WeightType>& flattenSensoryData(const DuckSensoryData& sensory)
    {
        int index = 0;

        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int x = 0; x < GRID_SIZE; ++x) {
                for (int material = 0; material < NUM_MATERIALS; ++material) {
                    input_buffer[index++] =
                        static_cast<WeightType>(sensory.material_histograms[y][x][material]);
                }
            }
        }

        input_buffer[index++] = static_cast<WeightType>(sensory.velocity.x / 10.0);
        input_buffer[index++] = static_cast<WeightType>(sensory.velocity.y / 10.0);
        input_buffer[index++] =
            sensory.on_ground ? static_cast<WeightType>(1.0f) : static_cast<WeightType>(0.0f);
        input_buffer[index++] = static_cast<WeightType>(sensory.facing_x);

        DIRTSIM_ASSERT(index == INPUT_SIZE, "DuckNeuralNetBrain: Input size mismatch");

        return input_buffer;
    }

    const std::vector<WeightType>& forward(const std::vector<WeightType>& input)
    {
        std::copy(b_h.begin(), b_h.end(), hidden_buffer.begin());
        for (int i = 0; i < INPUT_SIZE; ++i) {
            const WeightType input_value = input[i];
            if (input_value == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_ih[i * HIDDEN_SIZE];
            for (int h = 0; h < HIDDEN_SIZE; ++h) {
                hidden_buffer[h] += input_value * weights[h];
            }
        }
        for (int h = 0; h < HIDDEN_SIZE; ++h) {
            hidden_buffer[h] = relu(hidden_buffer[h]);
        }

        std::copy(b_o.begin(), b_o.end(), output_buffer.begin());
        for (int h = 0; h < HIDDEN_SIZE; ++h) {
            const WeightType hidden_value = hidden_buffer[h];
            const WeightType* weights = &w_ho[h * OUTPUT_SIZE];
            for (int o = 0; o < OUTPUT_SIZE; ++o) {
                output_buffer[o] += hidden_value * weights[o];
            }
        }

        return output_buffer;
    }
};

DuckNeuralNetBrain::DuckNeuralNetBrain() : impl_(std::make_unique<Impl>())
{
    std::mt19937 rng(std::random_device{}());
    const Genome genome = randomGenome(rng);
    impl_->loadFromGenome(genome);
}

DuckNeuralNetBrain::DuckNeuralNetBrain(const Genome& genome) : impl_(std::make_unique<Impl>())
{
    impl_->loadFromGenome(genome);
}

DuckNeuralNetBrain::DuckNeuralNetBrain(uint32_t seed) : impl_(std::make_unique<Impl>())
{
    std::mt19937 rng(seed);
    const Genome genome = randomGenome(rng);
    impl_->loadFromGenome(genome);
}

DuckNeuralNetBrain::~DuckNeuralNetBrain() = default;

void DuckNeuralNetBrain::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    decisionTimerSeconds_ += deltaTime;
    if (decisionTimerSeconds_ >= kDecisionIntervalSeconds) {
        decisionTimerSeconds_ = 0.0;

        const auto& input = impl_->flattenSensoryData(sensory);
        const auto& output = impl_->forward(input);

        lastMoveX_ = static_cast<float>(std::tanh(output[0]));
        jumpLatch_ = output[1] > 0.0f;
    }

    bool should_jump = false;
    if (jumpLatch_ && sensory.on_ground) {
        should_jump = true;
        jumpLatch_ = false;
    }

    duck.setInput({ .move = { lastMoveX_, 0.0f }, .jump = should_jump });

    if (should_jump) {
        current_action_ = DuckAction::JUMP;
        return;
    }

    if (std::abs(lastMoveX_) < 0.2f) {
        current_action_ = DuckAction::WAIT;
        return;
    }

    current_action_ = lastMoveX_ < 0.0f ? DuckAction::RUN_LEFT : DuckAction::RUN_RIGHT;
}

Genome DuckNeuralNetBrain::getGenome() const
{
    return impl_->toGenome();
}

void DuckNeuralNetBrain::setGenome(const Genome& genome)
{
    impl_->loadFromGenome(genome);
}

Genome DuckNeuralNetBrain::randomGenome(std::mt19937& rng)
{
    Genome genome(static_cast<size_t>(TOTAL_WEIGHTS));

    const WeightType ih_stddev = std::sqrt(2.0f / (INPUT_SIZE + HIDDEN_SIZE));
    const WeightType ho_stddev = std::sqrt(2.0f / (HIDDEN_SIZE + OUTPUT_SIZE));

    std::normal_distribution<WeightType> ih_dist(0.0f, ih_stddev);
    std::normal_distribution<WeightType> ho_dist(0.0f, ho_stddev);

    int idx = 0;
    for (int i = 0; i < W_IH_SIZE; ++i) {
        genome.weights[idx++] = ih_dist(rng);
    }
    for (int i = 0; i < B_H_SIZE; ++i) {
        genome.weights[idx++] = 0.0f;
    }
    for (int i = 0; i < W_HO_SIZE; ++i) {
        genome.weights[idx++] = ho_dist(rng);
    }
    for (int i = 0; i < B_O_SIZE; ++i) {
        genome.weights[idx++] = 0.0f;
    }

    DIRTSIM_ASSERT(idx == TOTAL_WEIGHTS, "DuckNeuralNetBrain: Generated genome size mismatch");

    return genome;
}

bool DuckNeuralNetBrain::isGenomeCompatible(const Genome& genome)
{
    return genome.weights.size() == static_cast<size_t>(TOTAL_WEIGHTS);
}

} // namespace DirtSim
