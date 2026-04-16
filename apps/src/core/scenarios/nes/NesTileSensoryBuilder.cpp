#include "core/scenarios/nes/NesTileSensoryBuilder.h"

#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"
#include "core/scenarios/nes/NesTileTokenFrame.h"

#include <string>

namespace DirtSim {

namespace {

void applyBuilderInput(NesTileSensoryData& sensory, const NesTileSensoryBuilderInput& input)
{
    sensory.facingX = input.facingX;
    sensory.selfViewX = input.selfViewX;
    sensory.selfViewY = input.selfViewY;
    setNesTilePreviousControlFromControllerMask(sensory, input.controllerMask);
    sensory.specialSenses = input.specialSenses;
    sensory.energy = input.energy;
    sensory.health = input.health;
    sensory.deltaTimeSeconds = input.deltaTimeSeconds;
}

} // namespace

Result<NesTileSensoryData, std::string> makeNesTileSensoryDataFromPpuSnapshot(
    const NesPpuSnapshot& ppuSnapshot,
    NesTileTokenizer& tokenizer,
    const NesTileSensoryBuilderInput& input)
{
    return makeNesTileSensoryDataFromTileFrame(makeNesTileFrame(ppuSnapshot), tokenizer, input);
}

Result<NesTileSensoryData, std::string> makeNesTileSensoryDataFromTileFrame(
    const NesTileFrame& tileFrame,
    NesTileTokenizer& tokenizer,
    const NesTileSensoryBuilderInput& input)
{
    const auto tokenFrameResult = makeNesTileTokenFrame(tileFrame, tokenizer);
    if (tokenFrameResult.isError()) {
        return Result<NesTileSensoryData, std::string>::error(
            "NesTileSensoryBuilder: Failed to tokenize tile frame: "
            + tokenFrameResult.errorValue());
    }

    NesTileSensoryData sensory;
    sensory.tileFrame = makeNesPlayerRelativeTileFrame(
        tokenFrameResult.value(), input.playerScreenX, input.playerScreenY);
    applyBuilderInput(sensory, input);
    return Result<NesTileSensoryData, std::string>::okay(sensory);
}

} // namespace DirtSim
