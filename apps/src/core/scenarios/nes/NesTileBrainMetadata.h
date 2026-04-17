#pragma once

#include "core/Result.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {

class NesTileTokenizer;

struct NesTileBrainCompatibilityMetadata {
    static constexpr int CurrentSchemaVersion = 1;

    int schemaVersion = CurrentSchemaVersion;
    int tileVocabularySize = 0;
    int tileEmbeddingDim = 0;
    int relativeTileColumns = 0;
    int relativeTileRows = 0;
    int scalarInputSize = 0;
    int h1Size = 0;
    int h2Size = 0;
    int outputSize = 0;
    uint16_t voidTokenId = 0;
    std::string tokenizerVocabularyHash;

    using serialize = zpp::bits::members<11>;
};

NesTileBrainCompatibilityMetadata makeNesTileBrainCompatibilityMetadata(
    const NesTileTokenizer& tokenizer);

Result<std::monostate, std::string> validateNesTileBrainCompatibilityMetadata(
    const NesTileBrainCompatibilityMetadata& actual,
    const NesTileBrainCompatibilityMetadata& expected);

void to_json(nlohmann::json& j, const NesTileBrainCompatibilityMetadata& metadata);
void from_json(const nlohmann::json& j, NesTileBrainCompatibilityMetadata& metadata);

} // namespace DirtSim
