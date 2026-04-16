#include "core/scenarios/nes/NesTileRecurrentBrain.h"

#include "core/Assert.h"
#include "core/organisms/brains/WeightType.h"
#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"
#include "core/scenarios/nes/NesTileSensoryData.h"
#include "core/scenarios/nes/NesTileTokenizer.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace DirtSim {

namespace {

constexpr int TILE_VOCAB_SIZE = NesTileRecurrentBrain::TileVocabularySize;
constexpr int TILE_EMBED_DIM = NesTileRecurrentBrain::TileEmbeddingDim;
constexpr int RELATIVE_TILE_COLUMNS = NesTileRecurrentBrain::RelativeTileColumns;
constexpr int RELATIVE_TILE_ROWS = NesTileRecurrentBrain::RelativeTileRows;
constexpr int VISUAL_INPUT_SIZE = NesTileRecurrentBrain::VisualInputSize;
constexpr int SCALAR_INPUT_SIZE = NesTileRecurrentBrain::ScalarInputSize;
constexpr int INPUT_SIZE = NesTileRecurrentBrain::InputSize;
constexpr int H1_SIZE = NesTileRecurrentBrain::H1Size;
constexpr int H2_SIZE = NesTileRecurrentBrain::H2Size;
constexpr int OUTPUT_SIZE = NesTileRecurrentBrain::OutputSize;

static_assert(TILE_VOCAB_SIZE == NesTileTokenizer::DefaultVocabSize);
static_assert(RELATIVE_TILE_COLUMNS == NesPlayerRelativeTileFrame::RelativeTileColumns);
static_assert(RELATIVE_TILE_ROWS == NesPlayerRelativeTileFrame::RelativeTileRows);

constexpr int TILE_EMBEDDING_SIZE = TILE_VOCAB_SIZE * TILE_EMBED_DIM;
constexpr int W_XH1_SIZE = INPUT_SIZE * H1_SIZE;
constexpr int W_H1H1_SIZE = H1_SIZE * H1_SIZE;
constexpr int B_H1_SIZE = H1_SIZE;
constexpr int ALPHA1_LOGIT_SIZE = H1_SIZE;
constexpr int W_H1H2_SIZE = H1_SIZE * H2_SIZE;
constexpr int W_H2H2_SIZE = H2_SIZE * H2_SIZE;
constexpr int B_H2_SIZE = H2_SIZE;
constexpr int ALPHA2_LOGIT_SIZE = H2_SIZE;
constexpr int ACTIVATION_LEAK_LOGIT_SIZE = 2;
constexpr int W_H2O_SIZE = H2_SIZE * OUTPUT_SIZE;
constexpr int B_O_SIZE = OUTPUT_SIZE;
constexpr int TOTAL_WEIGHTS = TILE_EMBEDDING_SIZE + W_XH1_SIZE + W_H1H1_SIZE + B_H1_SIZE
    + ALPHA1_LOGIT_SIZE + W_H1H2_SIZE + W_H2H2_SIZE + B_H2_SIZE + ALPHA2_LOGIT_SIZE
    + ACTIVATION_LEAK_LOGIT_SIZE + W_H2O_SIZE + B_O_SIZE;

constexpr WeightType HIDDEN_STATE_CLAMP_ABS = 3.0f;
constexpr WeightType HIDDEN_LEAK_ALPHA_MIN = 0.02f;
constexpr WeightType HIDDEN_LEAK_ALPHA_MAX = 0.98f;
constexpr WeightType HIDDEN_LEAK_ALPHA_LOGIT_INIT = -1.3862944f;
constexpr WeightType LEAKY_RELU_NEGATIVE_SLOPE_MIN = 0.02f;
constexpr WeightType LEAKY_RELU_NEGATIVE_SLOPE_MAX = 0.3f;
constexpr WeightType LEAKY_RELU_NEGATIVE_SLOPE_INIT = 0.1f;
constexpr WeightType LEAKY_RELU_NEGATIVE_SLOPE_LOGIT_INIT = -0.91629076f;

WeightType leakyRelu(WeightType x, WeightType negativeSlope)
{
    return x >= 0.0f ? x : (negativeSlope * x);
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

WeightType sigmoidToRange(WeightType x, WeightType minValue, WeightType maxValue)
{
    const WeightType unit = sigmoid(x);
    const WeightType scaled = minValue + ((maxValue - minValue) * unit);
    return std::clamp(scaled, minValue, maxValue);
}

} // namespace

struct NesTileRecurrentBrain::Impl {
    std::vector<WeightType> tile_embedding;
    std::vector<WeightType> w_xh1;
    std::vector<WeightType> w_h1h1;
    std::vector<WeightType> b_h1;
    std::vector<WeightType> alpha1_logit;
    std::vector<WeightType> alpha1;

    std::vector<WeightType> w_h1h2;
    std::vector<WeightType> w_h2h2;
    std::vector<WeightType> b_h2;
    std::vector<WeightType> alpha2_logit;
    std::vector<WeightType> alpha2;
    WeightType h1NegativeSlopeLogit = LEAKY_RELU_NEGATIVE_SLOPE_LOGIT_INIT;
    WeightType h1NegativeSlope = LEAKY_RELU_NEGATIVE_SLOPE_INIT;
    WeightType h2NegativeSlopeLogit = LEAKY_RELU_NEGATIVE_SLOPE_LOGIT_INIT;
    WeightType h2NegativeSlope = LEAKY_RELU_NEGATIVE_SLOPE_INIT;

    std::vector<WeightType> w_h2o;
    std::vector<WeightType> b_o;

    std::vector<WeightType> input_buffer;
    std::vector<WeightType> h1_buffer;
    std::vector<WeightType> h1_state;
    std::vector<WeightType> h2_buffer;
    std::vector<WeightType> h2_state;
    std::vector<WeightType> output_buffer;

    Impl()
        : tile_embedding(TILE_EMBEDDING_SIZE, 0.0f),
          w_xh1(W_XH1_SIZE, 0.0f),
          w_h1h1(W_H1H1_SIZE, 0.0f),
          b_h1(B_H1_SIZE, 0.0f),
          alpha1_logit(ALPHA1_LOGIT_SIZE, HIDDEN_LEAK_ALPHA_LOGIT_INIT),
          alpha1(ALPHA1_LOGIT_SIZE, 0.0f),
          w_h1h2(W_H1H2_SIZE, 0.0f),
          w_h2h2(W_H2H2_SIZE, 0.0f),
          b_h2(B_H2_SIZE, 0.0f),
          alpha2_logit(ALPHA2_LOGIT_SIZE, HIDDEN_LEAK_ALPHA_LOGIT_INIT),
          alpha2(ALPHA2_LOGIT_SIZE, 0.0f),
          w_h2o(W_H2O_SIZE, 0.0f),
          b_o(B_O_SIZE, 0.0f),
          input_buffer(INPUT_SIZE, 0.0f),
          h1_buffer(H1_SIZE, 0.0f),
          h1_state(H1_SIZE, 0.0f),
          h2_buffer(H2_SIZE, 0.0f),
          h2_state(H2_SIZE, 0.0f),
          output_buffer(OUTPUT_SIZE, 0.0f)
    {}

    void loadFromGenome(const Genome& genome)
    {
        DIRTSIM_ASSERT(
            genome.weights.size() == static_cast<size_t>(TOTAL_WEIGHTS),
            "NesTileRecurrentBrain: Genome weight count mismatch");

        int idx = 0;
        for (int i = 0; i < TILE_EMBEDDING_SIZE; ++i) {
            tile_embedding[i] = genome.weights[idx++];
        }
        for (int i = 0; i < W_XH1_SIZE; ++i) {
            w_xh1[i] = genome.weights[idx++];
        }
        for (int i = 0; i < W_H1H1_SIZE; ++i) {
            w_h1h1[i] = genome.weights[idx++];
        }
        for (int i = 0; i < B_H1_SIZE; ++i) {
            b_h1[i] = genome.weights[idx++];
        }
        for (int i = 0; i < ALPHA1_LOGIT_SIZE; ++i) {
            alpha1_logit[i] = genome.weights[idx++];
            alpha1[i] =
                std::clamp(sigmoid(alpha1_logit[i]), HIDDEN_LEAK_ALPHA_MIN, HIDDEN_LEAK_ALPHA_MAX);
        }
        h1NegativeSlopeLogit = genome.weights[idx++];
        h1NegativeSlope = sigmoidToRange(
            h1NegativeSlopeLogit, LEAKY_RELU_NEGATIVE_SLOPE_MIN, LEAKY_RELU_NEGATIVE_SLOPE_MAX);
        for (int i = 0; i < W_H1H2_SIZE; ++i) {
            w_h1h2[i] = genome.weights[idx++];
        }
        for (int i = 0; i < W_H2H2_SIZE; ++i) {
            w_h2h2[i] = genome.weights[idx++];
        }
        for (int i = 0; i < B_H2_SIZE; ++i) {
            b_h2[i] = genome.weights[idx++];
        }
        for (int i = 0; i < ALPHA2_LOGIT_SIZE; ++i) {
            alpha2_logit[i] = genome.weights[idx++];
            alpha2[i] =
                std::clamp(sigmoid(alpha2_logit[i]), HIDDEN_LEAK_ALPHA_MIN, HIDDEN_LEAK_ALPHA_MAX);
        }
        h2NegativeSlopeLogit = genome.weights[idx++];
        h2NegativeSlope = sigmoidToRange(
            h2NegativeSlopeLogit, LEAKY_RELU_NEGATIVE_SLOPE_MIN, LEAKY_RELU_NEGATIVE_SLOPE_MAX);
        for (int i = 0; i < W_H2O_SIZE; ++i) {
            w_h2o[i] = genome.weights[idx++];
        }
        for (int i = 0; i < B_O_SIZE; ++i) {
            b_o[i] = genome.weights[idx++];
        }

        std::fill(h1_state.begin(), h1_state.end(), 0.0f);
        std::fill(h2_state.begin(), h2_state.end(), 0.0f);
    }

    Genome toGenome() const
    {
        Genome genome(static_cast<size_t>(TOTAL_WEIGHTS));
        int idx = 0;

        for (int i = 0; i < TILE_EMBEDDING_SIZE; ++i) {
            genome.weights[idx++] = tile_embedding[i];
        }
        for (int i = 0; i < W_XH1_SIZE; ++i) {
            genome.weights[idx++] = w_xh1[i];
        }
        for (int i = 0; i < W_H1H1_SIZE; ++i) {
            genome.weights[idx++] = w_h1h1[i];
        }
        for (int i = 0; i < B_H1_SIZE; ++i) {
            genome.weights[idx++] = b_h1[i];
        }
        for (int i = 0; i < ALPHA1_LOGIT_SIZE; ++i) {
            genome.weights[idx++] = alpha1_logit[i];
        }
        genome.weights[idx++] = h1NegativeSlopeLogit;
        for (int i = 0; i < W_H1H2_SIZE; ++i) {
            genome.weights[idx++] = w_h1h2[i];
        }
        for (int i = 0; i < W_H2H2_SIZE; ++i) {
            genome.weights[idx++] = w_h2h2[i];
        }
        for (int i = 0; i < B_H2_SIZE; ++i) {
            genome.weights[idx++] = b_h2[i];
        }
        for (int i = 0; i < ALPHA2_LOGIT_SIZE; ++i) {
            genome.weights[idx++] = alpha2_logit[i];
        }
        genome.weights[idx++] = h2NegativeSlopeLogit;
        for (int i = 0; i < W_H2O_SIZE; ++i) {
            genome.weights[idx++] = w_h2o[i];
        }
        for (int i = 0; i < B_O_SIZE; ++i) {
            genome.weights[idx++] = b_o[i];
        }

        return genome;
    }

    const std::vector<WeightType>& flattenSensoryData(const NesTileSensoryData& sensory)
    {
        int index = 0;
        for (const auto token : sensory.tileFrame.tokens) {
            DIRTSIM_ASSERT(
                token < TILE_VOCAB_SIZE, "NesTileRecurrentBrain: Tile token out of range");
            const WeightType* embedding =
                &tile_embedding[static_cast<size_t>(token) * TILE_EMBED_DIM];
            for (int dim = 0; dim < TILE_EMBED_DIM; ++dim) {
                input_buffer[index++] = embedding[dim];
            }
        }

        input_buffer[index++] = 0.0f;
        input_buffer[index++] = 0.0f;
        input_buffer[index++] = 0.0f;
        input_buffer[index++] = static_cast<WeightType>(sensory.facingX);
        input_buffer[index++] = static_cast<WeightType>(sensory.selfViewX);
        input_buffer[index++] = static_cast<WeightType>(sensory.selfViewY);
        input_buffer[index++] = static_cast<WeightType>(sensory.previousControlX);
        input_buffer[index++] = static_cast<WeightType>(sensory.previousControlY);
        input_buffer[index++] =
            sensory.previousA ? static_cast<WeightType>(1.0f) : static_cast<WeightType>(0.0f);
        input_buffer[index++] =
            sensory.previousB ? static_cast<WeightType>(1.0f) : static_cast<WeightType>(0.0f);
        for (double sense : sensory.specialSenses) {
            input_buffer[index++] = static_cast<WeightType>(sense);
        }
        input_buffer[index++] = static_cast<WeightType>(sensory.energy);
        input_buffer[index++] = static_cast<WeightType>(sensory.health);

        DIRTSIM_ASSERT(index == INPUT_SIZE, "NesTileRecurrentBrain: Input size mismatch");

        return input_buffer;
    }

    const std::vector<WeightType>& forward(const std::vector<WeightType>& input)
    {
        std::copy(b_h1.begin(), b_h1.end(), h1_buffer.begin());
        for (int i = 0; i < INPUT_SIZE; ++i) {
            const WeightType inputValue = input[i];
            if (inputValue == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_xh1[i * H1_SIZE];
            for (int h = 0; h < H1_SIZE; ++h) {
                h1_buffer[h] += inputValue * weights[h];
            }
        }

        for (int i = 0; i < H1_SIZE; ++i) {
            const WeightType recurrentValue = h1_state[i];
            if (recurrentValue == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_h1h1[i * H1_SIZE];
            for (int h = 0; h < H1_SIZE; ++h) {
                h1_buffer[h] += recurrentValue * weights[h];
            }
        }

        for (int h = 0; h < H1_SIZE; ++h) {
            h1_buffer[h] = leakyRelu(h1_buffer[h], h1NegativeSlope);
        }

        for (int h = 0; h < H1_SIZE; ++h) {
            const WeightType candidate =
                std::clamp(h1_buffer[h], -HIDDEN_STATE_CLAMP_ABS, HIDDEN_STATE_CLAMP_ABS);
            const WeightType learnedAlpha = alpha1[h];
            const WeightType blended =
                ((1.0f - learnedAlpha) * h1_state[h]) + (learnedAlpha * candidate);
            h1_state[h] = std::clamp(blended, -HIDDEN_STATE_CLAMP_ABS, HIDDEN_STATE_CLAMP_ABS);
        }

        std::copy(b_h2.begin(), b_h2.end(), h2_buffer.begin());
        for (int i = 0; i < H1_SIZE; ++i) {
            const WeightType inputValue = h1_state[i];
            if (inputValue == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_h1h2[i * H2_SIZE];
            for (int h = 0; h < H2_SIZE; ++h) {
                h2_buffer[h] += inputValue * weights[h];
            }
        }

        for (int i = 0; i < H2_SIZE; ++i) {
            const WeightType recurrentValue = h2_state[i];
            if (recurrentValue == 0.0f) {
                continue;
            }
            const WeightType* weights = &w_h2h2[i * H2_SIZE];
            for (int h = 0; h < H2_SIZE; ++h) {
                h2_buffer[h] += recurrentValue * weights[h];
            }
        }

        for (int h = 0; h < H2_SIZE; ++h) {
            h2_buffer[h] = leakyRelu(h2_buffer[h], h2NegativeSlope);
        }

        for (int h = 0; h < H2_SIZE; ++h) {
            const WeightType candidate =
                std::clamp(h2_buffer[h], -HIDDEN_STATE_CLAMP_ABS, HIDDEN_STATE_CLAMP_ABS);
            const WeightType learnedAlpha = alpha2[h];
            const WeightType blended =
                ((1.0f - learnedAlpha) * h2_state[h]) + (learnedAlpha * candidate);
            h2_state[h] = std::clamp(blended, -HIDDEN_STATE_CLAMP_ABS, HIDDEN_STATE_CLAMP_ABS);
        }

        std::copy(b_o.begin(), b_o.end(), output_buffer.begin());
        for (int h = 0; h < H2_SIZE; ++h) {
            const WeightType hiddenValue = h2_state[h];
            const WeightType* weights = &w_h2o[h * OUTPUT_SIZE];
            for (int o = 0; o < OUTPUT_SIZE; ++o) {
                output_buffer[o] += hiddenValue * weights[o];
            }
        }

        return output_buffer;
    }
};

NesTileRecurrentBrain::NesTileRecurrentBrain(const Genome& genome) : impl_(std::make_unique<Impl>())
{
    impl_->loadFromGenome(genome);
}

NesTileRecurrentBrain::~NesTileRecurrentBrain() = default;

NesTileRecurrentBrain::NesTileRecurrentBrain(NesTileRecurrentBrain&&) noexcept = default;
NesTileRecurrentBrain& NesTileRecurrentBrain::operator=(NesTileRecurrentBrain&&) noexcept = default;

ControllerOutput NesTileRecurrentBrain::inferControllerOutput(const NesTileSensoryData& sensory)
{
    const auto& input = impl_->flattenSensoryData(sensory);
    const auto& output = impl_->forward(input);

    const float xRaw = static_cast<float>(output[0]);
    const float yRaw = static_cast<float>(output[1]);
    const float aRaw = static_cast<float>(output[2]);
    const float bRaw = static_cast<float>(output[3]);

    return ControllerOutput{
        .x = static_cast<float>(std::tanh(output[0])),
        .y = static_cast<float>(std::tanh(output[1])),
        .a = output[2] > 0.0f,
        .b = output[3] > 0.0f,
        .xRaw = xRaw,
        .yRaw = yRaw,
        .aRaw = aRaw,
        .bRaw = bRaw,
    };
}

Genome NesTileRecurrentBrain::getGenome() const
{
    return impl_->toGenome();
}

void NesTileRecurrentBrain::setGenome(const Genome& genome)
{
    impl_->loadFromGenome(genome);
}

Genome NesTileRecurrentBrain::randomGenome(std::mt19937& rng)
{
    Genome genome(static_cast<size_t>(TOTAL_WEIGHTS));

    const WeightType xh1Stddev = std::sqrt(2.0f / (INPUT_SIZE + H1_SIZE));
    const WeightType h1h1Stddev = std::sqrt(2.0f / (H1_SIZE + H1_SIZE));
    const WeightType h1h2Stddev = std::sqrt(2.0f / (H1_SIZE + H2_SIZE));
    const WeightType h2h2Stddev = std::sqrt(2.0f / (H2_SIZE + H2_SIZE));
    const WeightType h2oStddev = std::sqrt(2.0f / (H2_SIZE + OUTPUT_SIZE));

    std::normal_distribution<WeightType> embeddingDist(0.0f, 0.1f);
    std::normal_distribution<WeightType> xh1Dist(0.0f, xh1Stddev);
    std::normal_distribution<WeightType> h1h1Dist(0.0f, h1h1Stddev);
    std::normal_distribution<WeightType> h1h2Dist(0.0f, h1h2Stddev);
    std::normal_distribution<WeightType> h2h2Dist(0.0f, h2h2Stddev);
    std::normal_distribution<WeightType> h2oDist(0.0f, h2oStddev);
    std::uniform_real_distribution<WeightType> alphaLogitDist(-4.0f, 4.0f);

    int idx = 0;
    for (int i = 0; i < TILE_EMBEDDING_SIZE; ++i) {
        genome.weights[idx++] = embeddingDist(rng);
    }
    for (int i = 0; i < W_XH1_SIZE; ++i) {
        genome.weights[idx++] = xh1Dist(rng);
    }
    for (int i = 0; i < W_H1H1_SIZE; ++i) {
        genome.weights[idx++] = h1h1Dist(rng);
    }
    for (int i = 0; i < B_H1_SIZE; ++i) {
        genome.weights[idx++] = 0.0f;
    }
    for (int i = 0; i < ALPHA1_LOGIT_SIZE; ++i) {
        genome.weights[idx++] = alphaLogitDist(rng);
    }
    genome.weights[idx++] = LEAKY_RELU_NEGATIVE_SLOPE_LOGIT_INIT;
    for (int i = 0; i < W_H1H2_SIZE; ++i) {
        genome.weights[idx++] = h1h2Dist(rng);
    }
    for (int i = 0; i < W_H2H2_SIZE; ++i) {
        genome.weights[idx++] = h2h2Dist(rng);
    }
    for (int i = 0; i < B_H2_SIZE; ++i) {
        genome.weights[idx++] = 0.0f;
    }
    for (int i = 0; i < ALPHA2_LOGIT_SIZE; ++i) {
        genome.weights[idx++] = alphaLogitDist(rng);
    }
    genome.weights[idx++] = LEAKY_RELU_NEGATIVE_SLOPE_LOGIT_INIT;
    for (int i = 0; i < W_H2O_SIZE; ++i) {
        genome.weights[idx++] = h2oDist(rng);
    }
    for (int i = 0; i < B_O_SIZE; ++i) {
        genome.weights[idx++] = 0.0f;
    }

    DIRTSIM_ASSERT(idx == TOTAL_WEIGHTS, "NesTileRecurrentBrain: Generated genome size mismatch");
    return genome;
}

bool NesTileRecurrentBrain::isGenomeCompatible(const Genome& genome)
{
    return genome.weights.size() == static_cast<size_t>(TOTAL_WEIGHTS);
}

GenomeLayout NesTileRecurrentBrain::getGenomeLayout()
{
    return GenomeLayout{
        .segments = {
            { "tile_embedding", TILE_EMBEDDING_SIZE },
            { "input_h1", W_XH1_SIZE },
            { "h1_recurrent", W_H1H1_SIZE + B_H1_SIZE + ALPHA1_LOGIT_SIZE + 1 },
            { "h1_to_h2", W_H1H2_SIZE },
            { "h2_recurrent", W_H2H2_SIZE + B_H2_SIZE + ALPHA2_LOGIT_SIZE + 1 },
            { "output", W_H2O_SIZE + B_O_SIZE },
        },
    };
}

} // namespace DirtSim
