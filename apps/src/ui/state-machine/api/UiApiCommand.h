#pragma once

#include "DrawDebugToggle.h"
#include "Exit.h"
#include "GenomeBrowserOpen.h"
#include "GenomeDetailLoad.h"
#include "GenomeDetailOpen.h"
#include "IconSelect.h"
#include "MouseDown.h"
#include "MouseMove.h"
#include "MouseUp.h"
#include "PixelRendererToggle.h"
#include "PlantSeed.h"
#include "RenderModeSelect.h"
#include "ScreenGrab.h"
#include "SimPause.h"
#include "SimRun.h"
#include "SimStop.h"
#include "StateGet.h"
#include "StatusGet.h"
#include "StreamStart.h"
#include "TrainingResultDiscard.h"
#include "TrainingResultSave.h"
#include "TrainingStart.h"
#include "WebRtcAnswer.h"
#include "WebRtcCandidate.h"
#include "WebSocketAccessSet.h"
#include <variant>

namespace DirtSim {
namespace Ui {

/**
 * @brief Variant containing all UI API command types.
 */
using UiApiCommand = std::variant<
    UiApi::DrawDebugToggle::Command,
    UiApi::Exit::Command,
    UiApi::GenomeBrowserOpen::Command,
    UiApi::GenomeDetailLoad::Command,
    UiApi::GenomeDetailOpen::Command,
    UiApi::IconSelect::Command,
    UiApi::MouseDown::Command,
    UiApi::MouseMove::Command,
    UiApi::MouseUp::Command,
    UiApi::PlantSeed::Command,
    UiApi::PixelRendererToggle::Command,
    UiApi::RenderModeSelect::Command,
    UiApi::ScreenGrab::Command,
    UiApi::SimPause::Command,
    UiApi::SimRun::Command,
    UiApi::SimStop::Command,
    UiApi::StateGet::Command,
    UiApi::StatusGet::Command,
    UiApi::StreamStart::Command,
    UiApi::TrainingResultDiscard::Command,
    UiApi::TrainingResultSave::Command,
    UiApi::TrainingStart::Command,
    UiApi::WebSocketAccessSet::Command,
    UiApi::WebRtcAnswer::Command,
    UiApi::WebRtcCandidate::Command>;

} // namespace Ui
} // namespace DirtSim
