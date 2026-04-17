#include "core/scenarios/nes/NesTileBrainMetadata.h"

#include "core/ReflectSerializer.h"
#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"
#include "core/scenarios/nes/NesTileRecurrentBrain.h"
#include "core/scenarios/nes/NesTileTokenizer.h"

#include <sstream>

namespace DirtSim {

namespace {

template <typename T>
std::string mismatchMessage(const char* field, const T& actual, const T& expected)
{
    std::ostringstream stream;
    stream << "NES tile brain compatibility mismatch for " << field << ": checkpoint=" << actual
           << ", current=" << expected;
    return stream.str();
}

} // namespace

NesTileBrainCompatibilityMetadata makeNesTileBrainCompatibilityMetadata(
    const NesTileTokenizer& tokenizer)
{
    return NesTileBrainCompatibilityMetadata{
        .schemaVersion = NesTileBrainCompatibilityMetadata::CurrentSchemaVersion,
        .tileVocabularySize = NesTileRecurrentBrain::TileVocabularySize,
        .tileEmbeddingDim = NesTileRecurrentBrain::TileEmbeddingDim,
        .relativeTileColumns = NesPlayerRelativeTileFrame::RelativeTileColumns,
        .relativeTileRows = NesPlayerRelativeTileFrame::RelativeTileRows,
        .scalarInputSize = NesTileRecurrentBrain::ScalarInputSize,
        .h1Size = NesTileRecurrentBrain::H1Size,
        .h2Size = NesTileRecurrentBrain::H2Size,
        .outputSize = NesTileRecurrentBrain::OutputSize,
        .voidTokenId = NesTileTokenizer::VoidToken,
        .tokenizerVocabularyHash = tokenizer.getVocabularyHash(),
    };
}

Result<std::monostate, std::string> validateNesTileBrainCompatibilityMetadata(
    const NesTileBrainCompatibilityMetadata& actual,
    const NesTileBrainCompatibilityMetadata& expected)
{
    if (actual.schemaVersion != expected.schemaVersion) {
        return Result<std::monostate, std::string>::error(
            mismatchMessage("schemaVersion", actual.schemaVersion, expected.schemaVersion));
    }
    if (actual.tileVocabularySize != expected.tileVocabularySize) {
        return Result<std::monostate, std::string>::error(mismatchMessage(
            "tileVocabularySize", actual.tileVocabularySize, expected.tileVocabularySize));
    }
    if (actual.tileEmbeddingDim != expected.tileEmbeddingDim) {
        return Result<std::monostate, std::string>::error(mismatchMessage(
            "tileEmbeddingDim", actual.tileEmbeddingDim, expected.tileEmbeddingDim));
    }
    if (actual.relativeTileColumns != expected.relativeTileColumns) {
        return Result<std::monostate, std::string>::error(mismatchMessage(
            "relativeTileColumns", actual.relativeTileColumns, expected.relativeTileColumns));
    }
    if (actual.relativeTileRows != expected.relativeTileRows) {
        return Result<std::monostate, std::string>::error(mismatchMessage(
            "relativeTileRows", actual.relativeTileRows, expected.relativeTileRows));
    }
    if (actual.scalarInputSize != expected.scalarInputSize) {
        return Result<std::monostate, std::string>::error(
            mismatchMessage("scalarInputSize", actual.scalarInputSize, expected.scalarInputSize));
    }
    if (actual.h1Size != expected.h1Size) {
        return Result<std::monostate, std::string>::error(
            mismatchMessage("h1Size", actual.h1Size, expected.h1Size));
    }
    if (actual.h2Size != expected.h2Size) {
        return Result<std::monostate, std::string>::error(
            mismatchMessage("h2Size", actual.h2Size, expected.h2Size));
    }
    if (actual.outputSize != expected.outputSize) {
        return Result<std::monostate, std::string>::error(
            mismatchMessage("outputSize", actual.outputSize, expected.outputSize));
    }
    if (actual.voidTokenId != expected.voidTokenId) {
        return Result<std::monostate, std::string>::error(
            mismatchMessage("voidTokenId", actual.voidTokenId, expected.voidTokenId));
    }
    if (actual.tokenizerVocabularyHash != expected.tokenizerVocabularyHash) {
        return Result<std::monostate, std::string>::error(mismatchMessage(
            "tokenizerVocabularyHash",
            actual.tokenizerVocabularyHash,
            expected.tokenizerVocabularyHash));
    }

    return Result<std::monostate, std::string>::okay({});
}

void to_json(nlohmann::json& j, const NesTileBrainCompatibilityMetadata& metadata)
{
    j = ReflectSerializer::to_json(metadata);
}

void from_json(const nlohmann::json& j, NesTileBrainCompatibilityMetadata& metadata)
{
    metadata = ReflectSerializer::from_json<NesTileBrainCompatibilityMetadata>(j);
}

} // namespace DirtSim
