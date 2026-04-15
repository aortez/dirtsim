#include "core/scenarios/nes/NesTileTokenFrame.h"

#include <cstddef>
#include <string>

namespace DirtSim {

Result<NesTileTokenFrame, std::string> makeNesTileTokenFrame(
    const NesTileFrame& tileFrame, NesTileTokenizer& tokenizer)
{
    NesTileTokenFrame tokenFrame{
        .frameId = tileFrame.frameId,
        .scrollX = tileFrame.scrollX,
        .scrollY = tileFrame.scrollY,
    };

    for (size_t cellIndex = 0; cellIndex < tileFrame.tilePatternHashes.size(); ++cellIndex) {
        const auto tokenResult = tokenizer.tokenForHash(tileFrame.tilePatternHashes[cellIndex]);
        if (tokenResult.isError()) {
            return Result<NesTileTokenFrame, std::string>::error(
                "NesTileTokenFrame: Failed to tokenize cell " + std::to_string(cellIndex) + ": "
                + tokenResult.errorValue());
        }

        tokenFrame.tokens[cellIndex] = tokenResult.value();
    }

    return Result<NesTileTokenFrame, std::string>::okay(tokenFrame);
}

} // namespace DirtSim
