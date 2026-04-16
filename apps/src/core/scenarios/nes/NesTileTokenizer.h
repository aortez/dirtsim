#pragma once

#include "core/Result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {

class NesTileTokenizer final {
public:
    using TilePatternHash = uint64_t;
    using TileToken = uint16_t;
    using TileIdPatternHashTable = std::array<TilePatternHash, 256u>;
    using TileIdTokenRemap = std::array<TileToken, 256u>;

    enum class Mode : uint8_t {
        Learning = 0,
        Frozen = 1,
    };

    static constexpr TileToken DefaultVocabSize = 512u;
    static constexpr TileToken VoidToken = 0u;

    explicit NesTileTokenizer(TileToken vocabSize = DefaultVocabSize);

    Result<size_t, std::string> buildVocabulary(const TileIdPatternHashTable& tilePatternHashes);
    Result<size_t, std::string> buildVocabulary(std::vector<TilePatternHash> tilePatternHashes);
    void freeze();
    Result<TileIdTokenRemap, std::string> tileIdTokenRemapBuild(
        const TileIdPatternHashTable& tilePatternHashes);
    Result<TileToken, std::string> tokenForHash(std::optional<TilePatternHash> patternHash);

    Mode getMode() const { return mode_; }
    TileToken getVocabSize() const { return vocabSize_; }
    size_t getMappedHashCount() const { return hashToToken_.size(); }
    void reset();

private:
    std::unordered_map<TilePatternHash, TileToken> hashToToken_;
    Mode mode_ = Mode::Learning;
    TileToken nextToken_ = VoidToken + 1u;
    TileToken vocabSize_ = DefaultVocabSize;
};

} // namespace DirtSim
