#include "core/scenarios/nes/NesTileTokenizer.h"

#include "core/Assert.h"

#include <string>

namespace DirtSim {

NesTileTokenizer::NesTileTokenizer(TileToken vocabSize) : vocabSize_(vocabSize)
{
    DIRTSIM_ASSERT(vocabSize_ > VoidToken + 1u, "NesTileTokenizer: vocabSize must be at least 2");
}

Result<NesTileTokenizer::TileIdTokenRemap, std::string> NesTileTokenizer::tileIdTokenRemapBuild(
    const TileIdPatternHashTable& tilePatternHashes)
{
    TileIdTokenRemap remap{};

    for (size_t tileId = 0; tileId < tilePatternHashes.size(); ++tileId) {
        const auto tokenResult = tokenForHash(tilePatternHashes[tileId]);
        if (tokenResult.isError()) {
            return Result<TileIdTokenRemap, std::string>::error(
                "NesTileTokenizer: Failed to map tile id " + std::to_string(tileId) + ": "
                + tokenResult.errorValue());
        }

        remap[tileId] = tokenResult.value();
    }

    return Result<TileIdTokenRemap, std::string>::okay(remap);
}

Result<NesTileTokenizer::TileToken, std::string> NesTileTokenizer::tokenForHash(
    std::optional<TilePatternHash> patternHash)
{
    if (!patternHash.has_value()) {
        return Result<TileToken, std::string>::okay(VoidToken);
    }

    const auto it = hashToToken_.find(patternHash.value());
    if (it != hashToToken_.end()) {
        return Result<TileToken, std::string>::okay(it->second);
    }

    if (nextToken_ >= vocabSize_) {
        return Result<TileToken, std::string>::error(
            "NesTileTokenizer: Tile token vocabulary exhausted after "
            + std::to_string(hashToToken_.size()) + " unique patterns with vocab size "
            + std::to_string(vocabSize_));
    }

    const TileToken assignedToken = nextToken_;
    hashToToken_.emplace(patternHash.value(), assignedToken);
    ++nextToken_;
    return Result<TileToken, std::string>::okay(assignedToken);
}

void NesTileTokenizer::reset()
{
    hashToToken_.clear();
    nextToken_ = VoidToken + 1u;
}

} // namespace DirtSim
