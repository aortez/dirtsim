#pragma once

#include "ui/controls/IconRail.h"

#include "api/DrawDebugToggle.h"
#include "api/Exit.h"
#include "api/MouseDown.h"
#include "api/MouseMove.h"
#include "api/MouseUp.h"
#include "api/PixelRendererToggle.h"
#include "api/RenderModeSelect.h"
#include "api/ScreenGrab.h"
#include "api/SimPause.h"
#include "api/SimRun.h"
#include "api/SimStop.h"
#include "api/StatusGet.h"
#include "api/StreamStart.h"
#include "api/WebRtcAnswer.h"
#include "api/WebRtcCandidate.h"
#include "core/PhysicsSettings.h"
#include "core/api/UiUpdateEvent.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/TrainingResultAvailable.h"
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
 * @brief User clicked Start button in Training state to begin evolution.
 */
struct StartEvolutionButtonClickedEvent {
    EvolutionConfig evolution;
    MutationConfig mutation;
    TrainingSpec training;
    static constexpr const char* name() { return "StartEvolutionButtonClickedEvent"; }
};

/**
 * @brief User clicked Stop button in Training state.
 */
struct StopTrainingClickedEvent {
    static constexpr const char* name() { return "StopTrainingClickedEvent"; }
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
    static constexpr const char* name() { return "TrainingResultSaveClickedEvent"; }
};

struct TrainingResultDiscardClickedEvent {
    static constexpr const char* name() { return "TrainingResultDiscardClickedEvent"; }
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
 * @brief Training results received from server after evolution completes.
 */
struct TrainingResultAvailableReceivedEvent {
    Api::TrainingResultAvailable result;
    static constexpr const char* name() { return "TrainingResultAvailableReceivedEvent"; }
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

/**
 * @brief IconRail auto-shrink timer fired (requests minimization after inactivity).
 */
struct RailAutoShrinkRequestEvent {
    static constexpr const char* name() { return "RailAutoShrinkRequestEvent"; }
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
    StartEvolutionButtonClickedEvent,
    StopTrainingClickedEvent,
    QuitTrainingClickedEvent,
    TrainButtonClickedEvent,
    NextFractalClickedEvent,
    ViewBestButtonClickedEvent,
    TrainingResultSaveClickedEvent,
    TrainingResultDiscardClickedEvent,
    RequestWorldUpdateCommand,

    // Server data updates
    DirtSim::UiUpdateEvent,
    EvolutionProgressReceivedEvent,
    TrainingResultAvailableReceivedEvent,
    PhysicsSettingsReceivedEvent,

    // UI control events
    IconSelectedEvent,
    RailAutoShrinkRequestEvent,
    RailModeChangedEvent,

    // API commands (local from LVGL or remote from WebSocket)
    DirtSim::UiApi::DrawDebugToggle::Cwc,
    DirtSim::UiApi::Exit::Cwc,
    DirtSim::UiApi::MouseDown::Cwc,
    DirtSim::UiApi::MouseMove::Cwc,
    DirtSim::UiApi::MouseUp::Cwc,
    DirtSim::UiApi::PixelRendererToggle::Cwc,
    DirtSim::UiApi::RenderModeSelect::Cwc,
    DirtSim::UiApi::ScreenGrab::Cwc,
    DirtSim::UiApi::SimPause::Cwc,
    DirtSim::UiApi::SimRun::Cwc,
    DirtSim::UiApi::SimStop::Cwc,
    DirtSim::UiApi::StatusGet::Cwc,
    DirtSim::UiApi::StreamStart::Cwc,
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
