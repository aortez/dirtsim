#pragma once

#include "ui/controls/IconRail.h"

#include "api/DrawDebugToggle.h"
#include "api/Exit.h"
#include "api/GenomeBrowserOpen.h"
#include "api/GenomeDetailLoad.h"
#include "api/GenomeDetailOpen.h"
#include "api/IconRailExpand.h"
#include "api/IconRailShowIcons.h"
#include "api/IconSelect.h"
#include "api/MouseDown.h"
#include "api/MouseMove.h"
#include "api/MouseUp.h"
#include "api/PixelRendererToggle.h"
#include "api/PlantSeed.h"
#include "api/RenderModeSelect.h"
#include "api/ScreenGrab.h"
#include "api/SimPause.h"
#include "api/SimRun.h"
#include "api/SimStop.h"
#include "api/StateGet.h"
#include "api/StatusGet.h"
#include "api/StopButtonPress.h"
#include "api/StreamStart.h"
#include "api/SynthKeyEvent.h"
#include "api/TrainingConfigShowEvolution.h"
#include "api/TrainingQuit.h"
#include "api/TrainingResultDiscard.h"
#include "api/TrainingResultSave.h"
#include "api/WebRtcAnswer.h"
#include "api/WebRtcCandidate.h"
#include "core/PhysicsSettings.h"
#include "core/api/UiUpdateEvent.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/UserSettings.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/TrainingBestSnapshot.h"
#include "server/api/TrainingResult.h"
#include "ui/state-machine/api/TrainingStart.h"
#include <concepts>
#include <string>
#include <variant>
#include <vector>

namespace DirtSim {
namespace Ui {

/**
 * @brief Event definitions for the UI state machine.
 *
 * Events include lifecycle, server connection, and API commands.
 * Mouse events are API commands - both local (from LVGL) and remote (from WebSocket)
 * use the same API, ensuring consistent behavior.
 */

// =================================================================
// EVENT NAME CONCEPT
// =================================================================

/**
 * @brief Concept for events that have a name() method.
 */
template <typename T>
concept HasEventName = requires {
    { T::name() } -> std::convertible_to<const char*>;
};

// =================================================================
// LIFECYCLE EVENTS
// =================================================================

/**
 * @brief Initialization complete.
 */
struct InitCompleteEvent {
    static constexpr const char* name() { return "InitCompleteEvent"; }
};

// =================================================================
// SERVER CONNECTION EVENTS
// =================================================================

/**
 * @brief Connect to DSSM server.
 */
struct ConnectToServerCommand {
    std::string host;
    uint16_t port;
    static constexpr const char* name() { return "ConnectToServerCommand"; }
};

/**
 * @brief Server connection established.
 */
struct ServerConnectedEvent {
    static constexpr const char* name() { return "ServerConnectedEvent"; }
};

/**
 * @brief Server connection lost.
 */
struct ServerDisconnectedEvent {
    std::string reason;
    static constexpr const char* name() { return "ServerDisconnectedEvent"; }
};

/**
 * @brief Request world update from DSSM server.
 */
struct RequestWorldUpdateCommand {
    static constexpr const char* name() { return "RequestWorldUpdateCommand"; }
};

/**
 * @brief User clicked Start button in StartMenu.
 */
struct StartButtonClickedEvent {
    static constexpr const char* name() { return "StartButtonClickedEvent"; }
};

/**
 * @brief StartMenu idle timeout reached (auto-launch clock scenario).
 */
struct StartMenuIdleTimeoutEvent {
    static constexpr const char* name() { return "StartMenuIdleTimeoutEvent"; }
};

/**
 * @brief User clicked Train button in StartMenu.
 */
struct TrainButtonClickedEvent {
    static constexpr const char* name() { return "TrainButtonClickedEvent"; }
};

/**
 * @brief User clicked Next Fractal button in StartMenu.
 */
struct NextFractalClickedEvent {
    static constexpr const char* name() { return "NextFractalClickedEvent"; }
};

/**
 * @brief User clicked Stop button to return to StartMenu.
 */
struct StopButtonClickedEvent {
    static constexpr const char* name() { return "StopButtonClickedEvent"; }
};

/**
 * @brief User clicked Start button in Training state to begin evolution.
 */
struct StartEvolutionButtonClickedEvent {
    EvolutionConfig evolution;
    MutationConfig mutation;
    TrainingSpec training;
    TrainingResumePolicy resumePolicy = TrainingResumePolicy::WarmFromBest;
    static constexpr const char* name() { return "StartEvolutionButtonClickedEvent"; }
};

/**
 * @brief User clicked Stop button in Training state.
 */
struct StopTrainingClickedEvent {
    static constexpr const char* name() { return "StopTrainingClickedEvent"; }
};

struct TrainingPauseResumeClickedEvent {
    static constexpr const char* name() { return "TrainingPauseResumeClickedEvent"; }
};

/**
 * @brief User clicked Quit button in Training state.
 */
struct QuitTrainingClickedEvent {
    static constexpr const char* name() { return "QuitTrainingClickedEvent"; }
};

struct ViewBestButtonClickedEvent {
    GenomeId genomeId;
    static constexpr const char* name() { return "ViewBestButtonClickedEvent"; }
};

struct TrainingResultSaveClickedEvent {
    std::vector<GenomeId> ids;
    bool restart = false;
    static constexpr const char* name() { return "TrainingResultSaveClickedEvent"; }
};

struct TrainingResultDiscardClickedEvent {
    static constexpr const char* name() { return "TrainingResultDiscardClickedEvent"; }
};

struct TrainingStreamConfigChangedEvent {
    int intervalMs = 0;
    static constexpr const char* name() { return "TrainingStreamConfigChangedEvent"; }
};
struct GenomeLoadClickedEvent {
    GenomeId genomeId;
    Scenario::EnumType scenarioId = Scenario::EnumType::Sandbox;
    static constexpr const char* name() { return "GenomeLoadClickedEvent"; }
};

struct GenomeAddToTrainingClickedEvent {
    GenomeId genomeId;
    Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
    static constexpr const char* name() { return "GenomeAddToTrainingClickedEvent"; }
};

/**
 * @brief Physics settings received from server.
 */
struct PhysicsSettingsReceivedEvent {
    PhysicsSettings settings;
    static constexpr const char* name() { return "PhysicsSettingsReceivedEvent"; }
};

/**
 * @brief Evolution progress received from server (broadcast during training).
 */
struct EvolutionProgressReceivedEvent {
    Api::EvolutionProgress progress;
    static constexpr const char* name() { return "EvolutionProgressReceivedEvent"; }
};

/**
 * @brief Best snapshot received from server (new all-time fitness).
 */
struct TrainingBestSnapshotReceivedEvent {
    Api::TrainingBestSnapshot snapshot;
    static constexpr const char* name() { return "TrainingBestSnapshotReceivedEvent"; }
};

struct UserSettingsUpdatedEvent {
    DirtSim::UserSettings settings;
    static constexpr const char* name() { return "UserSettingsUpdatedEvent"; }
};

// =================================================================
// UI CONTROL EVENTS
// =================================================================

/**
 * @brief Icon selected/deselected in IconRail.
 */
struct IconSelectedEvent {
    IconId selectedId;
    IconId previousId;
    static constexpr const char* name() { return "IconSelectedEvent"; }
};

/**
 * @brief IconRail mode changed (Normal <-> Minimized).
 */
struct RailModeChangedEvent {
    RailMode newMode;
    static constexpr const char* name() { return "RailModeChangedEvent"; }
};

// =================================================================
// EVENT VARIANT
// =================================================================

/**
 * @brief Variant containing all UI event types.
 */
using Event = std::variant<
    // Lifecycle
    InitCompleteEvent,

    // Server connection
    ConnectToServerCommand,
    ServerConnectedEvent,
    ServerDisconnectedEvent,
    StartButtonClickedEvent,
    StartMenuIdleTimeoutEvent,
    StartEvolutionButtonClickedEvent,
    StopTrainingClickedEvent,
    TrainingPauseResumeClickedEvent,
    QuitTrainingClickedEvent,
    TrainButtonClickedEvent,
    NextFractalClickedEvent,
    StopButtonClickedEvent,
    ViewBestButtonClickedEvent,
    TrainingResultSaveClickedEvent,
    TrainingResultDiscardClickedEvent,
    TrainingStreamConfigChangedEvent,
    GenomeLoadClickedEvent,
    GenomeAddToTrainingClickedEvent,
    RequestWorldUpdateCommand,

    // Server data updates
    DirtSim::UiUpdateEvent,
    EvolutionProgressReceivedEvent,
    UserSettingsUpdatedEvent,
    TrainingBestSnapshotReceivedEvent,
    PhysicsSettingsReceivedEvent,

    // UI control events
    IconSelectedEvent,
    RailModeChangedEvent,

    // API commands (local from LVGL or remote from WebSocket)
    DirtSim::Api::TrainingResult::Cwc,
    DirtSim::UiApi::DrawDebugToggle::Cwc,
    DirtSim::UiApi::Exit::Cwc,
    DirtSim::UiApi::GenomeBrowserOpen::Cwc,
    DirtSim::UiApi::GenomeDetailLoad::Cwc,
    DirtSim::UiApi::GenomeDetailOpen::Cwc,
    DirtSim::UiApi::IconRailExpand::Cwc,
    DirtSim::UiApi::IconRailShowIcons::Cwc,
    DirtSim::UiApi::IconSelect::Cwc,
    DirtSim::UiApi::MouseDown::Cwc,
    DirtSim::UiApi::MouseMove::Cwc,
    DirtSim::UiApi::MouseUp::Cwc,
    DirtSim::UiApi::PlantSeed::Cwc,
    DirtSim::UiApi::PixelRendererToggle::Cwc,
    DirtSim::UiApi::RenderModeSelect::Cwc,
    DirtSim::UiApi::ScreenGrab::Cwc,
    DirtSim::UiApi::SimPause::Cwc,
    DirtSim::UiApi::SimRun::Cwc,
    DirtSim::UiApi::SimStop::Cwc,
    DirtSim::UiApi::StateGet::Cwc,
    DirtSim::UiApi::StatusGet::Cwc,
    DirtSim::UiApi::StopButtonPress::Cwc,
    DirtSim::UiApi::StreamStart::Cwc,
    DirtSim::UiApi::SynthKeyEvent::Cwc,
    DirtSim::UiApi::TrainingConfigShowEvolution::Cwc,
    DirtSim::UiApi::TrainingQuit::Cwc,
    DirtSim::UiApi::TrainingResultDiscard::Cwc,
    DirtSim::UiApi::TrainingResultSave::Cwc,
    DirtSim::UiApi::TrainingStart::Cwc,
    DirtSim::UiApi::WebRtcAnswer::Cwc,
    DirtSim::UiApi::WebRtcCandidate::Cwc>;

/**
 * @brief Helper to get event name from variant.
 */
inline std::string getEventName(const Event& event)
{
    return std::visit([](auto&& e) { return std::string(e.name()); }, event);
}

} // namespace Ui
} // namespace DirtSim
