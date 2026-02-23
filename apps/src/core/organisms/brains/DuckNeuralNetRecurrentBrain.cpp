#include "DuckNeuralNetRecurrentBrain.h"

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
constexpr int SPECIAL_SENSE_COUNT = DuckSensoryData::SPECIAL_SENSE_COUNT;

constexpr int INPUT_HISTOGRAM_SIZE = GRID_SIZE * GRID_SIZE * NUM_MATERIALS;
constexpr int INPUT_SIZE = INPUT_HISTOGRAM_SIZE + 4 + SPECIAL_SENSE_COUNT;
constexpr int HIDDEN_SIZE = 32;
constexpr int OUTPUT_SIZE = 2;

constexpr int W_IH_SIZE = INPUT_SIZE * HIDDEN_SIZE;
constexpr int W_HH_SIZE = HIDDEN_SIZE * HIDDEN_SIZE;
constexpr int B_H_SIZE = HIDDEN_SIZE;
constexpr int W_HO_SIZE = HIDDEN_SIZE * OUTPUT_SIZE;
constexpr int B_O_SIZE = OUTPUT_SIZE;
constexpr int ALPHA_LOGIT_SIZE = HIDDEN_SIZE;
constexpr int TOTAL_WEIGHTS =
    W_IH_SIZE + W_HH_SIZE + B_H_SIZE + W_HO_SIZE + B_O_SIZE + ALPHA_LOGIT_SIZE;
constexpr WeightType HIDDEN_STATE_CLAMP_ABS = 3.0f;
constexpr WeightType HIDDEN_LEAK_ALPHA_MIN = 0.02f;
constexpr WeightType HIDDEN_LEAK_ALPHA_MAX = 0.98f;
constexpr WeightType HIDDEN_LEAK_ALPHA_LOGIT_INIT = -1.3862944f; // logit(0.2).

WeightType relu(WeightType x)
{
    return std::max(static_cast<WeightType>(0.0f), x);
}

WeightType sigmoid(WeightType x)
{
    if (x >= 0.0f) {
        const WeightType z = std::exp(-x);
        return 1.0f / (1.0f + z);
    }

    const WeightType z = std::exp(x);
    return z / (1.0f + z);
}

} // namespace

struct DuckNeuralNetRecurrentBrain::Impl {
    std::vector<WeightType> w_ih;
    std::vector<WeightType> w_hh;
    std::vector<WeightType> b_h;
    std::vector<WeightType> w_ho;
    std::vector<WeightType> b_o;
    std::vector<WeightType> alpha_logit;
    std::vector<WeightType> input_buffer;
    std::vector<WeightType> hidden_buffer;
    std::vector<WeightType> hidden_state;
    std::vector<WeightType> output_buffer;

    Impl()
        : w_ih(W_IH_SIZE, 0.0f),
          w_hh(W_HH_SIZE, 0.0f),
          b_h(B_H_SIZE, 0.0f),
          w_ho(W_HO_SIZE, 0.0f),
          b_o(B_O_SIZE, 0.0f),
          alpha_logit(ALPHA_LOGIT_SIZE, HIDDEN_LEAK_ALPHA_LOGIT_INIT),
          input_buffer(INPUT_SIZE, 0.0f),
          hidden_buffer(HIDDEN_SIZE, 0.0f),
          hidden_state(HIDDEN_SIZE, 0.0f),
          output_buffer(OUTPUT_SIZE, 0.0f)
    {}

    void loadFromGenome(const Genome& genome)
    {
        DIRTSIM_ASSERT(
            genome.weights.size() == TOTAL_WEIGHTS,
            "DuckNeuralNetRecurrentBrain: Genome weight count mismatch");

        int idx = 0;
        for (int i = 0; i < W_IH_SIZE; ++i) {
            w_ih[i] = genome.weights[idx++];
        }
        for (int i = 0; i < W_HH_SIZE; ++i) {
            w_hh[i] = genome.weights[idx++];
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
        for (int i = 0; i < ALPHA_LOGIT_SIZE; ++i) {
            alpha_logit[i] = genome.weights[idx++];
        }
        std::fill(hidden_state.begin(), hidden_state.end(), 0.0f);
    }

    Genome toGenome() const
    {
        Genome genome(static_cast<size_t>(TOTAL_WEIGHTS));
        int idx = 0;

        for (int i = 0; i < W_IH_SIZE; ++i) {
            genome.weights[idx++] = w_ih[i];
        }
        for (int i = 0; i < W_HH_SIZE; ++i) {
            genome.weights[idx++] = w_hh[i];
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
        for (int i = 0; i < ALPHA_LOGIT_SIZE; ++i) {
            genome.weights[idx++] = alpha_logit[i];
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
        for (int sense = 0; sense < SPECIAL_SENSE_COUNT; ++sense) {
            input_buffer[index++] = static_cast<WeightType>(sensory.special_senses[sense]);
        }

        DIRTSIM_ASSERT(index == INPUT_SIZE, "DuckNeuralNetRecurrentBrain: Input size mismatch");

        return input_buffer;
    }

    const std::vector<WeightType>& forward(const std::vector<WeightType>& input)
    {
        std::copy(b_h.begin(), b_h.end(), hidden_buffer.begin());
        for (int i = 0; i < INPUT_SIZE; ++i) {
            const WeightType inputValue = input[i];
            if (inputValue == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_ih[i * HIDDEN_SIZE];
            for (int h = 0; h < HIDDEN_SIZE; ++h) {
                hidden_buffer[h] += inputValue * weights[h];
            }
        }

        for (int i = 0; i < HIDDEN_SIZE; ++i) {
            const WeightType recurrentValue = hidden_state[i];
            if (recurrentValue == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_hh[i * HIDDEN_SIZE];
            for (int h = 0; h < HIDDEN_SIZE; ++h) {
                hidden_buffer[h] += recurrentValue * weights[h];
            }
        }

        for (int h = 0; h < HIDDEN_SIZE; ++h) {
            hidden_buffer[h] = relu(hidden_buffer[h]);
        }

        for (int h = 0; h < HIDDEN_SIZE; ++h) {
            const WeightType candidate =
                std::clamp(hidden_buffer[h], -HIDDEN_STATE_CLAMP_ABS, HIDDEN_STATE_CLAMP_ABS);
            const WeightType learnedAlpha =
                std::clamp(sigmoid(alpha_logit[h]), HIDDEN_LEAK_ALPHA_MIN, HIDDEN_LEAK_ALPHA_MAX);
            const WeightType blended =
                ((1.0f - learnedAlpha) * hidden_state[h]) + (learnedAlpha * candidate);
            hidden_state[h] = std::clamp(blended, -HIDDEN_STATE_CLAMP_ABS, HIDDEN_STATE_CLAMP_ABS);
        }

        std::copy(b_o.begin(), b_o.end(), output_buffer.begin());
        for (int h = 0; h < HIDDEN_SIZE; ++h) {
            const WeightType hiddenValue = hidden_state[h];
            const WeightType* weights = &w_ho[h * OUTPUT_SIZE];
            for (int o = 0; o < OUTPUT_SIZE; ++o) {
                output_buffer[o] += hiddenValue * weights[o];
            }
        }
        return output_buffer;
    }
};

DuckNeuralNetRecurrentBrain::DuckNeuralNetRecurrentBrain() : impl_(std::make_unique<Impl>())
{
    std::mt19937 rng(std::random_device{}());
    const Genome genome = randomGenome(rng);
    impl_->loadFromGenome(genome);
}

DuckNeuralNetRecurrentBrain::DuckNeuralNetRecurrentBrain(const Genome& genome)
    : impl_(std::make_unique<Impl>())
{
    impl_->loadFromGenome(genome);
}

DuckNeuralNetRecurrentBrain::DuckNeuralNetRecurrentBrain(uint32_t seed)
    : impl_(std::make_unique<Impl>())
{
    std::mt19937 rng(seed);
    const Genome genome = randomGenome(rng);
    impl_->loadFromGenome(genome);
}

DuckNeuralNetRecurrentBrain::~DuckNeuralNetRecurrentBrain() = default;

void DuckNeuralNetRecurrentBrain::think(
    Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    (void)deltaTime;
    duck.setInput(inferInput(sensory));
}

DuckInput DuckNeuralNetRecurrentBrain::inferInput(const DuckSensoryData& sensory)
{
    const auto& input = impl_->flattenSensoryData(sensory);
    const auto& output = impl_->forward(input);

    lastMoveX_ = static_cast<float>(std::tanh(output[0]));
    jumpHeld_ = output[1] > 0.0f;

    const DuckInput duckInput{ .move = { lastMoveX_, 0.0f }, .jump = jumpHeld_ };

    if (jumpHeld_ && sensory.on_ground) {
        current_action_ = DuckAction::JUMP;
        return duckInput;
    }

    if (std::abs(lastMoveX_) <= 0.05f) {
        current_action_ = DuckAction::WAIT;
        return duckInput;
    }

    current_action_ = lastMoveX_ < 0.0f ? DuckAction::RUN_LEFT : DuckAction::RUN_RIGHT;
    return duckInput;
}

Genome DuckNeuralNetRecurrentBrain::getGenome() const
{
    return impl_->toGenome();
}

void DuckNeuralNetRecurrentBrain::setGenome(const Genome& genome)
{
    impl_->loadFromGenome(genome);
}

Genome DuckNeuralNetRecurrentBrain::randomGenome(std::mt19937& rng)
{
    Genome genome(static_cast<size_t>(TOTAL_WEIGHTS));

    const WeightType ihStddev = std::sqrt(2.0f / (INPUT_SIZE + HIDDEN_SIZE));
    const WeightType hhStddev = std::sqrt(2.0f / (HIDDEN_SIZE + HIDDEN_SIZE));
    const WeightType hoStddev = std::sqrt(2.0f / (HIDDEN_SIZE + OUTPUT_SIZE));

    std::normal_distribution<WeightType> ihDist(0.0f, ihStddev);
    std::normal_distribution<WeightType> hhDist(0.0f, hhStddev);
    std::normal_distribution<WeightType> hoDist(0.0f, hoStddev);

    int idx = 0;
    for (int i = 0; i < W_IH_SIZE; ++i) {
        genome.weights[idx++] = ihDist(rng);
    }
    for (int i = 0; i < W_HH_SIZE; ++i) {
        genome.weights[idx++] = hhDist(rng);
    }
    for (int i = 0; i < B_H_SIZE; ++i) {
        genome.weights[idx++] = 0.0f;
    }
    for (int i = 0; i < W_HO_SIZE; ++i) {
        genome.weights[idx++] = hoDist(rng);
    }
    for (int i = 0; i < B_O_SIZE; ++i) {
        genome.weights[idx++] = 0.0f;
    }
    for (int i = 0; i < ALPHA_LOGIT_SIZE; ++i) {
        genome.weights[idx++] = HIDDEN_LEAK_ALPHA_LOGIT_INIT;
    }

    DIRTSIM_ASSERT(
        idx == TOTAL_WEIGHTS, "DuckNeuralNetRecurrentBrain: Generated genome size mismatch");
    return genome;
}

bool DuckNeuralNetRecurrentBrain::isGenomeCompatible(const Genome& genome)
{
    return genome.weights.size() == static_cast<size_t>(TOTAL_WEIGHTS);
}

} // namespace DirtSim
