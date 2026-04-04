#pragma once

#include "DebugVisualizationSelect.h"
#include "DrawDebugToggle.h"
#include "Exit.h"
#include "GenomeBrowserOpen.h"
#include "GenomeDetailLoad.h"
#include "GenomeDetailOpen.h"
#include "IconRailExpand.h"
#include "IconRailShowIcons.h"
#include "IconSelect.h"
#include "MouseDown.h"
#include "MouseMove.h"
#include "MouseUp.h"
#include "NetworkConnectCancelPress.h"
#include "NetworkConnectPress.h"
#include "NetworkDiagnosticsGet.h"
#include "NetworkPasswordSubmit.h"
#include "NetworkScannerEnterPress.h"
#include "NetworkScannerExitPress.h"
#include "PlanBrowserOpen.h"
#include "PlanDetailOpen.h"
#include "PlanDetailSelect.h"
#include "PlanPlaybackPauseSet.h"
#include "PlanPlaybackStart.h"
#include "PlanPlaybackStop.h"
#include "PlantSeed.h"
#include "RenderModeSelect.h"
#include "ScreenGrab.h"
#include "SearchPauseSet.h"
#include "SearchStart.h"
#include "SearchStop.h"
#include "SimPause.h"
#include "SimRun.h"
#include "SimStop.h"
#include "StateGet.h"
#include "StatusGet.h"
#include "StopButtonPress.h"
#include "StreamStart.h"
#include "SynthKeyEvent.h"
#include "TrainingActiveScenarioControlsShow.h"
#include "TrainingConfigShowEvolution.h"
#include "TrainingQuit.h"
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
    UiApi::DebugVisualizationSelect::Command,
    UiApi::DrawDebugToggle::Command,
    UiApi::Exit::Command,
    UiApi::GenomeBrowserOpen::Command,
    UiApi::GenomeDetailLoad::Command,
    UiApi::GenomeDetailOpen::Command,
    UiApi::IconRailExpand::Command,
    UiApi::IconRailShowIcons::Command,
    UiApi::IconSelect::Command,
    UiApi::MouseDown::Command,
    UiApi::MouseMove::Command,
    UiApi::MouseUp::Command,
    UiApi::NetworkConnectCancelPress::Command,
    UiApi::NetworkConnectPress::Command,
    UiApi::NetworkDiagnosticsGet::Command,
    UiApi::NetworkPasswordSubmit::Command,
    UiApi::NetworkScannerEnterPress::Command,
    UiApi::NetworkScannerExitPress::Command,
    UiApi::PlanBrowserOpen::Command,
    UiApi::PlanDetailOpen::Command,
    UiApi::PlanDetailSelect::Command,
    UiApi::PlanPlaybackPauseSet::Command,
    UiApi::PlanPlaybackStart::Command,
    UiApi::PlanPlaybackStop::Command,
    UiApi::PlantSeed::Command,
    UiApi::RenderModeSelect::Command,
    UiApi::ScreenGrab::Command,
    UiApi::SearchPauseSet::Command,
    UiApi::SearchStart::Command,
    UiApi::SearchStop::Command,
    UiApi::SimPause::Command,
    UiApi::SimRun::Command,
    UiApi::SimStop::Command,
    UiApi::StateGet::Command,
    UiApi::StatusGet::Command,
    UiApi::StopButtonPress::Command,
    UiApi::StreamStart::Command,
    UiApi::SynthKeyEvent::Command,
    UiApi::TrainingActiveScenarioControlsShow::Command,
    UiApi::TrainingConfigShowEvolution::Command,
    UiApi::TrainingQuit::Command,
    UiApi::TrainingResultDiscard::Command,
    UiApi::TrainingResultSave::Command,
    UiApi::TrainingStart::Command,
    UiApi::WebSocketAccessSet::Command,
    UiApi::WebRtcAnswer::Command,
    UiApi::WebRtcCandidate::Command>;

} // namespace Ui
} // namespace DirtSim
