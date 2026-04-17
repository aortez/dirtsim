#pragma once

#include "core/organisms/brains/ControllerOutput.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeLayout.h"

#include <memory>
#include <random>

namespace DirtSim {

struct NesTileSensoryData;

class NesTileRecurrentBrain final {
public:
    static constexpr int TileVocabularySize = 512;
    static constexpr int TileEmbeddingDim = 4;
    static constexpr int RelativeTileColumns = 63;
    static constexpr int RelativeTileRows = 55;
    static constexpr int VisualInputSize =
        RelativeTileColumns * RelativeTileRows * TileEmbeddingDim;
    static constexpr int ScalarInputSize = 44;
    static constexpr int InputSize = VisualInputSize + ScalarInputSize;
    static constexpr int H1Size = 64;
    static constexpr int H2Size = 32;
    static constexpr int OutputSize = 4;

    explicit NesTileRecurrentBrain(const Genome& genome);
    ~NesTileRecurrentBrain();

    NesTileRecurrentBrain(const NesTileRecurrentBrain&) = delete;
    NesTileRecurrentBrain& operator=(const NesTileRecurrentBrain&) = delete;
    NesTileRecurrentBrain(NesTileRecurrentBrain&&) noexcept;
    NesTileRecurrentBrain& operator=(NesTileRecurrentBrain&&) noexcept;

    ControllerOutput inferControllerOutput(const NesTileSensoryData& sensory);

    Genome getGenome() const;
    void setGenome(const Genome& genome);

    static Genome randomGenome(std::mt19937& rng);
    static bool isGenomeCompatible(const Genome& genome);
    static GenomeLayout getGenomeLayout();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace DirtSim
