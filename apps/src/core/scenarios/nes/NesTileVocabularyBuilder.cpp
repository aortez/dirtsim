#include "core/scenarios/nes/NesTileVocabularyBuilder.h"

#include "core/scenarios/nes/NesTileFrame.h"

namespace DirtSim {

void NesTileVocabularyBuilder::addFrame(const NesTileFrame& frame)
{
    tilePatternHashes_.insert(
        tilePatternHashes_.end(), frame.tilePatternHashes.begin(), frame.tilePatternHashes.end());
}

void NesTileVocabularyBuilder::addSnapshot(const NesPpuSnapshot& snapshot)
{
    addFrame(makeNesTileFrame(snapshot));
}

Result<NesTileVocabularyBuildResult, std::string> NesTileVocabularyBuilder::buildFrozenTokenizer(
    NesTileTokenizer& tokenizer) const
{
    if (tilePatternHashes_.empty()) {
        return Result<NesTileVocabularyBuildResult, std::string>::error(
            "NesTileVocabularyBuilder: Cannot build vocabulary without tile samples");
    }

    auto buildResult = tokenizer.buildVocabulary(tilePatternHashes_);
    if (buildResult.isError()) {
        return Result<NesTileVocabularyBuildResult, std::string>::error(
            "NesTileVocabularyBuilder: Failed to build vocabulary: " + buildResult.errorValue());
    }

    tokenizer.freeze();
    return Result<NesTileVocabularyBuildResult, std::string>::okay(
        NesTileVocabularyBuildResult{
            .sampledTileCount = tilePatternHashes_.size(),
            .uniquePatternCount = buildResult.value(),
        });
}

Result<NesTileTokenizer, std::string> NesTileVocabularyBuilder::buildFrozenTokenizer(
    NesTileTokenizer::TileToken vocabSize) const
{
    NesTileTokenizer tokenizer(vocabSize);
    auto buildResult = buildFrozenTokenizer(tokenizer);
    if (buildResult.isError()) {
        return Result<NesTileTokenizer, std::string>::error(buildResult.errorValue());
    }

    return Result<NesTileTokenizer, std::string>::okay(std::move(tokenizer));
}

void NesTileVocabularyBuilder::reset()
{
    tilePatternHashes_.clear();
}

} // namespace DirtSim
