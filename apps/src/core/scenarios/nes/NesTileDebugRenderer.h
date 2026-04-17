#pragma once

#include "core/RenderMessage.h"
#include "core/Result.h"
#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"
#include "core/scenarios/nes/NesTileDebugView.h"
#include "core/scenarios/nes/NesTileFrame.h"
#include "core/scenarios/nes/NesTileTokenFrame.h"
#include "core/scenarios/nes/NesTileTokenizer.h"

#include <string>
#include <vector>

namespace DirtSim {

struct NesTileDebugRenderImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

struct NesTileDebugRenderInput {
    const ScenarioVideoFrame* videoFrame = nullptr;
    const NesTileFrame* tileFrame = nullptr;
    const NesTileTokenFrame* tokenFrame = nullptr;
    const NesPlayerRelativeTileFrame* relativeFrame = nullptr;
};

Result<NesTileDebugRenderImage, std::string> makeNesTileDebugRenderImage(
    NesTileDebugView view, const NesTileDebugRenderInput& input);
ScenarioVideoFrame makeNesTileDebugScenarioVideoFrame(
    const NesTileDebugRenderImage& image, uint64_t frameId);
uint32_t nesTileDebugTokenColor(NesTileTokenizer::TileToken token);

} // namespace DirtSim
