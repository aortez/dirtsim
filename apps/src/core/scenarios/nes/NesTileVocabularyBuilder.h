#pragma once

#include "core/Result.h"
#include "core/scenarios/nes/NesTileTokenizer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace DirtSim {

struct NesPpuSnapshot;
struct NesTileFrame;

struct NesTileVocabularyBuildResult {
    size_t sampledTileCount = 0;
    size_t uniquePatternCount = 0;
};

class NesTileVocabularyBuilder final {
public:
    void addFrame(const NesTileFrame& frame);
    void addSnapshot(const NesPpuSnapshot& snapshot);
    Result<NesTileVocabularyBuildResult, std::string> buildFrozenTokenizer(
        NesTileTokenizer& tokenizer) const;
    Result<NesTileTokenizer, std::string> buildFrozenTokenizer(
        NesTileTokenizer::TileToken vocabSize = NesTileTokenizer::DefaultVocabSize) const;

    size_t getSampledTileCount() const { return tilePatternHashes_.size(); }
    void reset();

private:
    std::vector<NesTileTokenizer::TilePatternHash> tilePatternHashes_;
};

} // namespace DirtSim
