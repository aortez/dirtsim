#include "core/scenarios/nes/NesTileTokenizer.h"

#include "core/Assert.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace DirtSim {

NesTileTokenizer::NesTileTokenizer(TileToken vocabSize) : vocabSize_(vocabSize)
{
    DIRTSIM_ASSERT(vocabSize_ > VoidToken + 1u, "NesTileTokenizer: vocabSize must be at least 2");
}

Result<size_t, std::string> NesTileTokenizer::buildVocabulary(
    const TileIdPatternHashTable& tilePatternHashes)
{
    return buildVocabulary(
        std::vector<TilePatternHash>(tilePatternHashes.begin(), tilePatternHashes.end()));
}

Result<size_t, std::string> NesTileTokenizer::buildVocabulary(
    std::vector<TilePatternHash> tilePatternHashes)
{
    std::sort(tilePatternHashes.begin(), tilePatternHashes.end());
    tilePatternHashes.erase(
        std::unique(tilePatternHashes.begin(), tilePatternHashes.end()), tilePatternHashes.end());

    if (tilePatternHashes.size() >= static_cast<size_t>(vocabSize_)) {
        return Result<size_t, std::string>::error(
            "NesTileTokenizer: Tile token vocabulary exhausted while building "
            + std::to_string(tilePatternHashes.size()) + " unique patterns with vocab size "
            + std::to_string(vocabSize_));
    }

    std::unordered_map<TilePatternHash, TileToken> hashToToken;
    hashToToken.reserve(tilePatternHashes.size());

    TileToken nextToken = VoidToken + 1u;
    for (const TilePatternHash hash : tilePatternHashes) {
        hashToToken.emplace(hash, nextToken);
        ++nextToken;
    }

    hashToToken_ = std::move(hashToToken);
    nextToken_ = nextToken;
    mode_ = Mode::Learning;
    return Result<size_t, std::string>::okay(hashToToken_.size());
}

void NesTileTokenizer::freeze()
{
    mode_ = Mode::Frozen;
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

    if (mode_ == Mode::Frozen) {
        return Result<TileToken, std::string>::error(
            "NesTileTokenizer: Frozen vocabulary missing tile pattern hash "
            + std::to_string(patternHash.value()));
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
    mode_ = Mode::Learning;
    nextToken_ = VoidToken + 1u;
}

} // namespace DirtSim
