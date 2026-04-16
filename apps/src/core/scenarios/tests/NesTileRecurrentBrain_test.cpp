#include "core/scenarios/nes/NesTileRecurrentBrain.h"

#include "core/organisms/brains/WeightType.h"
#include "core/scenarios/nes/NesTileSensoryData.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <string>

using namespace DirtSim;

namespace {

constexpr float kMaxHiddenAlpha = 0.98f;
constexpr float kStrongPositiveLogit = 100.0f;
constexpr int kTileEmbeddingSize =
    NesTileRecurrentBrain::TileVocabularySize * NesTileRecurrentBrain::TileEmbeddingDim;
constexpr int kWXH1Size = NesTileRecurrentBrain::InputSize * NesTileRecurrentBrain::H1Size;
constexpr int kWH1H1Size = NesTileRecurrentBrain::H1Size * NesTileRecurrentBrain::H1Size;
constexpr int kBH1Size = NesTileRecurrentBrain::H1Size;
constexpr int kAlpha1LogitSize = NesTileRecurrentBrain::H1Size;
constexpr int kWH1H2Size = NesTileRecurrentBrain::H1Size * NesTileRecurrentBrain::H2Size;
constexpr int kWH2H2Size = NesTileRecurrentBrain::H2Size * NesTileRecurrentBrain::H2Size;
constexpr int kBH2Size = NesTileRecurrentBrain::H2Size;
constexpr int kAlpha2LogitSize = NesTileRecurrentBrain::H2Size;
constexpr int kWH2OSize = NesTileRecurrentBrain::H2Size * NesTileRecurrentBrain::OutputSize;
constexpr int kBOSize = NesTileRecurrentBrain::OutputSize;
constexpr int kTotalGenomeSize = kTileEmbeddingSize + kWXH1Size + kWH1H1Size + kBH1Size
    + kAlpha1LogitSize + 1 + kWH1H2Size + kWH2H2Size + kBH2Size + kAlpha2LogitSize + 1 + kWH2OSize
    + kBOSize;

size_t segmentOffset(const GenomeLayout& layout, const std::string& segmentName)
{
    size_t offset = 0u;
    for (const auto& segment : layout.segments) {
        if (segment.name == segmentName) {
            return offset;
        }
        offset += static_cast<size_t>(segment.size);
    }

    ADD_FAILURE() << "Missing genome segment: " << segmentName;
    return 0u;
}

size_t tileEmbeddingIndex(uint16_t token, int dim)
{
    return segmentOffset(NesTileRecurrentBrain::getGenomeLayout(), "tile_embedding")
        + static_cast<size_t>(token) * NesTileRecurrentBrain::TileEmbeddingDim
        + static_cast<size_t>(dim);
}

size_t wXh1Index(size_t inputIndex, size_t hiddenIndex)
{
    return segmentOffset(NesTileRecurrentBrain::getGenomeLayout(), "input_h1")
        + (inputIndex * NesTileRecurrentBrain::H1Size) + hiddenIndex;
}

size_t alpha1LogitIndex(size_t hiddenIndex)
{
    return segmentOffset(NesTileRecurrentBrain::getGenomeLayout(), "h1_recurrent")
        + static_cast<size_t>(kWH1H1Size + kBH1Size) + hiddenIndex;
}

size_t wH1H2Index(size_t h1Index, size_t h2Index)
{
    return segmentOffset(NesTileRecurrentBrain::getGenomeLayout(), "h1_to_h2")
        + (h1Index * NesTileRecurrentBrain::H2Size) + h2Index;
}

size_t alpha2LogitIndex(size_t hiddenIndex)
{
    return segmentOffset(NesTileRecurrentBrain::getGenomeLayout(), "h2_recurrent")
        + static_cast<size_t>(kWH2H2Size + kBH2Size) + hiddenIndex;
}

size_t wH2OIndex(size_t h2Index, size_t outputIndex)
{
    return segmentOffset(NesTileRecurrentBrain::getGenomeLayout(), "output")
        + (h2Index * NesTileRecurrentBrain::OutputSize) + outputIndex;
}

size_t outputBiasIndex(size_t outputIndex)
{
    return segmentOffset(NesTileRecurrentBrain::getGenomeLayout(), "output")
        + static_cast<size_t>(kWH2OSize) + outputIndex;
}

Genome makeZeroGenome()
{
    return Genome(static_cast<size_t>(kTotalGenomeSize), 0.0f);
}

Genome makeEmbeddingProbeGenome()
{
    Genome genome = makeZeroGenome();
    genome.weights[tileEmbeddingIndex(0u, 0)] = 0.75f;
    genome.weights[tileEmbeddingIndex(7u, 0)] = 2.25f;
    genome.weights[wXh1Index(0u, 0u)] = 1.0f;
    genome.weights[alpha1LogitIndex(0u)] = kStrongPositiveLogit;
    genome.weights[wH1H2Index(0u, 0u)] = 1.0f;
    genome.weights[alpha2LogitIndex(0u)] = kStrongPositiveLogit;
    genome.weights[wH2OIndex(0u, 0u)] = 1.0f;
    return genome;
}

} // namespace

TEST(NesTileRecurrentBrainTest, GenomeLayoutMatchesExpectedSegments)
{
    const GenomeLayout layout = NesTileRecurrentBrain::getGenomeLayout();

    ASSERT_EQ(layout.segments.size(), 6u);
    EXPECT_EQ(layout.segments[0].name, "tile_embedding");
    EXPECT_EQ(layout.segments[0].size, kTileEmbeddingSize);
    EXPECT_EQ(layout.segments[1].name, "input_h1");
    EXPECT_EQ(layout.segments[1].size, kWXH1Size);
    EXPECT_EQ(layout.segments[2].name, "h1_recurrent");
    EXPECT_EQ(layout.segments[2].size, kWH1H1Size + kBH1Size + kAlpha1LogitSize + 1);
    EXPECT_EQ(layout.segments[3].name, "h1_to_h2");
    EXPECT_EQ(layout.segments[3].size, kWH1H2Size);
    EXPECT_EQ(layout.segments[4].name, "h2_recurrent");
    EXPECT_EQ(layout.segments[4].size, kWH2H2Size + kBH2Size + kAlpha2LogitSize + 1);
    EXPECT_EQ(layout.segments[5].name, "output");
    EXPECT_EQ(layout.segments[5].size, kWH2OSize + kBOSize);
    EXPECT_EQ(layout.totalSize(), kTotalGenomeSize);
    EXPECT_EQ(layout.totalSize(), 899398);
}

TEST(NesTileRecurrentBrainTest, RandomGenomeMatchesLayoutSize)
{
    std::mt19937 rng(123u);

    const Genome genome = NesTileRecurrentBrain::randomGenome(rng);
    const GenomeLayout layout = NesTileRecurrentBrain::getGenomeLayout();

    EXPECT_EQ(genome.weights.size(), static_cast<size_t>(layout.totalSize()));
    EXPECT_TRUE(NesTileRecurrentBrain::isGenomeCompatible(genome));
}

TEST(NesTileRecurrentBrainTest, IncompatibleGenomeSizesFailCompatibility)
{
    EXPECT_FALSE(NesTileRecurrentBrain::isGenomeCompatible(Genome{}));
    EXPECT_FALSE(
        NesTileRecurrentBrain::isGenomeCompatible(
            Genome(static_cast<size_t>(kTotalGenomeSize - 1), 0.0f)));
    EXPECT_FALSE(
        NesTileRecurrentBrain::isGenomeCompatible(
            Genome(static_cast<size_t>(kTotalGenomeSize + 1), 0.0f)));
}

TEST(NesTileRecurrentBrainTest, TokenEmbeddingLookupUsesVoidAndTokenRows)
{
    const Genome genome = makeEmbeddingProbeGenome();
    const float expectedScale = kMaxHiddenAlpha * kMaxHiddenAlpha;

    NesTileRecurrentBrain voidBrain(genome);
    const ControllerOutput voidOutput = voidBrain.inferControllerOutput(NesTileSensoryData{});

    NesTileSensoryData tokenSensory;
    tokenSensory.tileFrame.tokens[0u] = 7u;
    NesTileRecurrentBrain tokenBrain(genome);
    const ControllerOutput tokenOutput = tokenBrain.inferControllerOutput(tokenSensory);

    EXPECT_NEAR(voidOutput.xRaw, 0.75f * expectedScale, 1e-5f);
    EXPECT_NEAR(tokenOutput.xRaw, 2.25f * expectedScale, 1e-5f);
}

TEST(NesTileRecurrentBrainTest, HandAuthoredGenomeProducesDeterministicControllerOutput)
{
    Genome genome = makeZeroGenome();
    genome.weights[outputBiasIndex(0u)] = 0.5f;
    genome.weights[outputBiasIndex(1u)] = -0.25f;
    genome.weights[outputBiasIndex(2u)] = 0.75f;
    genome.weights[outputBiasIndex(3u)] = -0.75f;

    NesTileRecurrentBrain brain(genome);
    const ControllerOutput output = brain.inferControllerOutput(NesTileSensoryData{});

    EXPECT_NEAR(output.xRaw, 0.5f, 1e-6f);
    EXPECT_NEAR(output.yRaw, -0.25f, 1e-6f);
    EXPECT_NEAR(output.x, std::tanh(0.5f), 1e-6f);
    EXPECT_NEAR(output.y, std::tanh(-0.25f), 1e-6f);
    EXPECT_TRUE(output.a);
    EXPECT_FALSE(output.b);
    EXPECT_NEAR(output.aRaw, 0.75f, 1e-6f);
    EXPECT_NEAR(output.bRaw, -0.75f, 1e-6f);
}
