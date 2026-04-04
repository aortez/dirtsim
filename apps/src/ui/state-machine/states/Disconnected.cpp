#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/StatusGet.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/LogPanel.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <lvgl/lvgl.h>
#include <nlohmann/json.hpp>

namespace DirtSim {
namespace Ui {
namespace State {

namespace {

constexpr int ICON_RAIL_WIDTH = 80;
constexpr int STATUS_HEIGHT = 60;
constexpr uint32_t BG_COLOR = 0x202020;
constexpr uint32_t RAIL_COLOR = 0x303030;

State::Any selectPostConnectState(StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    DIRTSIM_ASSERT(wsService.isConnected(), "WebSocket must be connected after connect");

    Api::StatusGet::Command statusCmd{};
    const auto statusResult =
        wsService.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, 2000);
    if (statusResult.isError()) {
        LOG_WARN(
            State,
            "StatusGet failed after connect: {}, defaulting to StartMenu",
            statusResult.errorValue());
        return StartMenu{};
    }

    if (statusResult.value().isError()) {
        LOG_WARN(
            State,
            "StatusGet returned error after connect: {}, defaulting to StartMenu",
            statusResult.value().errorValue().message);
        return StartMenu{};
    }

    const auto& status = statusResult.value().value();
    LOG_INFO(State, "Server status after connect: {}", status.state);

    if (status.state == "Evolution") {
        LOG_INFO(State, "Server is already evolving, transitioning to TrainingActive");
        return TrainingActive{};
    }
    if (status.state == "SearchActive") {
        LOG_INFO(State, "Server is already searching, transitioning to SearchActive");
        return SearchActive{};
    }
    if (status.state == "PlanPlayback") {
        LOG_INFO(State, "Server is already playing back a plan, transitioning to PlanPlayback");
        return PlanPlayback{};
    }

    LOG_INFO(State, "Server not in evolution, transitioning to StartMenu");
    return StartMenu{};
}

} // namespace

void Disconnected::onEnter(StateMachine& sm)
{
    sm_ = &sm;
    LOG_INFO(
        State,
        "Entered Disconnected state (retry_count={}, retry_pending={})",
        retry_count_,
        retry_pending_);

    createDiagnosticsScreen(sm);
}

void Disconnected::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting Disconnected state");

    logPanel_.reset();

    // Clear the container.
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");
    uiManager->clearCurrentContainer();

    mainContainer_ = nullptr;
    iconRail_ = nullptr;
    logButton_ = nullptr;
    contentArea_ = nullptr;
    statusLabel_ = nullptr;
}

void Disconnected::createDiagnosticsScreen(StateMachine& sm)
{
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    // Get the config container (dedicated screen for diagnostics).
    lv_obj_t* screen = uiManager->getDisconnectedDiagnosticsContainer();
    DIRTSIM_ASSERT(screen, "Disconnected state requires a diagnostics container");

    // Create main container with horizontal flex layout.
    mainContainer_ = lv_obj_create(screen);
    lv_obj_set_size(mainContainer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mainContainer_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(mainContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mainContainer_, 0, 0);
    lv_obj_set_style_pad_all(mainContainer_, 0, 0);
    lv_obj_set_flex_flow(mainContainer_, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(mainContainer_, LV_OBJ_FLAG_SCROLLABLE);

    // Create icon rail on the left.
    iconRail_ = lv_obj_create(mainContainer_);
    lv_obj_set_size(iconRail_, ICON_RAIL_WIDTH, LV_PCT(100));
    lv_obj_set_style_bg_color(iconRail_, lv_color_hex(RAIL_COLOR), 0);
    lv_obj_set_style_bg_opa(iconRail_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(iconRail_, 0, 0);
    lv_obj_set_style_pad_all(iconRail_, 4, 0);
    lv_obj_set_flex_flow(iconRail_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        iconRail_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(iconRail_, LV_OBJ_FLAG_SCROLLABLE);

    // Create log icon button.
    logButton_ = LVGLBuilder::actionButton(iconRail_)
                     .icon(LV_SYMBOL_LIST)
                     .mode(LVGLBuilder::ActionMode::Toggle)
                     .size(ICON_RAIL_WIDTH - 8)
                     .checked(true)
                     .glowColor(0x00aaff)
                     .textColor(0x00aaff)
                     .buildOrLog();

    // Create content area (right side).
    contentArea_ = lv_obj_create(mainContainer_);
    lv_obj_set_flex_grow(contentArea_, 1);
    lv_obj_set_height(contentArea_, LV_PCT(100));
    lv_obj_set_style_bg_color(contentArea_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(contentArea_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(contentArea_, 0, 0);
    lv_obj_set_style_pad_all(contentArea_, 8, 0);
    lv_obj_set_flex_flow(contentArea_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(contentArea_, LV_OBJ_FLAG_SCROLLABLE);

    // Create status label at top of content area.
    statusLabel_ = lv_label_create(contentArea_);
    lv_obj_set_width(statusLabel_, LV_PCT(100));
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xff6600), 0);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_bottom(statusLabel_, 8, 0);
    updateStatusLabel();

    // Create log panel container.
    lv_obj_t* logContainer = lv_obj_create(contentArea_);
    lv_obj_set_flex_grow(logContainer, 1);
    lv_obj_set_width(logContainer, LV_PCT(100));
    lv_obj_set_style_bg_opa(logContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logContainer, 0, 0);
    lv_obj_set_style_pad_all(logContainer, 0, 0);
    lv_obj_clear_flag(logContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Create log panel.
    logPanel_ = std::make_unique<LogPanel>(logContainer, "dirtsim.log", 30);
    logPanel_->setRefreshInterval(2.0);

    LOG_INFO(State, "Diagnostics screen created");
}

void Disconnected::updateStatusLabel()
{
    DIRTSIM_ASSERT(statusLabel_, "Disconnected state requires statusLabel_");

    std::string status;
    if (retry_pending_) {
        const double retryIntervalSeconds = getCurrentRetryIntervalSeconds();
        auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_attempt_time_).count();
        double remaining = retryIntervalSeconds - elapsed;
        if (remaining < 0) {
            remaining = 0;
        }

        status = "Unable to connect to server\n";
        if (retry_count_ < INITIAL_RETRY_ATTEMPTS) {
            status += "Retry " + std::to_string(retry_count_) + "/"
                + std::to_string(INITIAL_RETRY_ATTEMPTS);
            status += " in " + std::to_string(static_cast<int>(remaining + 0.5)) + "s...";
        }
        else {
            status += "Retrying every "
                + std::to_string(static_cast<int>(BACKGROUND_RETRY_INTERVAL_SECONDS + 0.5))
                + "s (attempt " + std::to_string(retry_count_) + ")\n";
            status += "Next retry in " + std::to_string(static_cast<int>(remaining + 0.5)) + "s...";
        }
    }
    else if (retry_count_ >= INITIAL_RETRY_ATTEMPTS) {
        status = "Connection unavailable\n";
        status += "Retrying every "
            + std::to_string(static_cast<int>(BACKGROUND_RETRY_INTERVAL_SECONDS + 0.5)) + "s";
    }
    else {
        status = "Connecting to server...";
    }

    lv_label_set_text(statusLabel_, status.c_str());
}

void Disconnected::updateAnimations()
{
    updateStatusLabel();

    if (connectInProgress_ || !retry_pending_) {
        return;
    }
    DIRTSIM_ASSERT(sm_, "Disconnected state requires a valid StateMachine");

    // Check if enough time has passed since last attempt.
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_attempt_time_).count();

    if (elapsed >= getCurrentRetryIntervalSeconds()) {
        const bool isInitialRetryPhase = retry_count_ < INITIAL_RETRY_ATTEMPTS;

        LOG_INFO(
            State,
            "Retrying connection to {}:{} ({} attempt {})",
            pending_host_,
            pending_port_,
            isInitialRetryPhase ? "initial" : "background",
            retry_count_ + 1);

        // Queue a new connection attempt.
        sm_->queueEvent(ConnectToServerCommand{ pending_host_, pending_port_ });
    }
}

double Disconnected::getCurrentRetryIntervalSeconds() const
{
    if (retry_count_ < INITIAL_RETRY_ATTEMPTS) {
        return INITIAL_RETRY_INTERVAL_SECONDS;
    }
    return BACKGROUND_RETRY_INTERVAL_SECONDS;
}

State::Any Disconnected::onEvent(const ConnectToServerCommand& cmd, StateMachine& sm)
{
    LOG_INFO(State, "Connect command received (host={}, port={})", cmd.host, cmd.port);
    sm.setLastServerAddress(cmd.host, cmd.port);

    if (connectInProgress_) {
        LOG_INFO(State, "Connect already in progress, ignoring duplicate command");
        return std::move(*this);
    }

    pending_host_ = cmd.host;
    pending_port_ = cmd.port;

    auto& wsService = sm.getWebSocketService();

    // NOW connect (after callbacks are registered).
    std::string url = "ws://" + cmd.host + ":" + std::to_string(cmd.port);
    auto connectResult = wsService.connect(url, 0);
    if (connectResult.isError()) {
        LOG_ERROR(State, "WebSocketService connection failed: {}", connectResult.errorValue());

        // Track retry state - preserve current state but enable retries.
        connectInProgress_ = false;
        retry_count_++;
        last_attempt_time_ = std::chrono::steady_clock::now();
        retry_pending_ = true;

        if (retry_count_ == INITIAL_RETRY_ATTEMPTS) {
            LOG_WARN(
                State,
                "Initial retry attempts exhausted ({}), continuing background retries every "
                "{:.0f}s",
                INITIAL_RETRY_ATTEMPTS,
                BACKGROUND_RETRY_INTERVAL_SECONDS);
        }
        LOG_INFO(
            State,
            "Will retry connection in {:.0f}s (attempt {})",
            getCurrentRetryIntervalSeconds(),
            retry_count_);

        updateStatusLabel();
        return std::move(*this);
    }

    // Connection initiated successfully; transition on ServerConnectedEvent.
    connectInProgress_ = true;
    retry_pending_ = false;
    LOG_INFO(
        State, "WebSocketService connect initiated to {}, waiting for ServerConnectedEvent", url);

    return std::move(*this);
}

State::Any Disconnected::onEvent(const ServerConnectedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Server connection established");

    auto& wsService = sm.getWebSocketService();
    DIRTSIM_ASSERT(wsService.isConnected(), "ServerConnectedEvent requires active connection");

    connectInProgress_ = false;
    retry_pending_ = false;
    retry_count_ = 0;

    return selectPostConnectState(sm);
}

State::Any Disconnected::onEvent(const ServerDisconnectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_WARN(State, "Connect failed while disconnected: {}", evt.reason);

    if (!connectInProgress_) {
        LOG_INFO(State, "Ignoring disconnect event while no connect attempt is in progress");
        return std::move(*this);
    }

    connectInProgress_ = false;
    retry_count_++;
    last_attempt_time_ = std::chrono::steady_clock::now();
    retry_pending_ = true;

    if (retry_count_ == INITIAL_RETRY_ATTEMPTS) {
        LOG_WARN(
            State,
            "Initial retry attempts exhausted ({}), continuing background retries every {:.0f}s",
            INITIAL_RETRY_ATTEMPTS,
            BACKGROUND_RETRY_INTERVAL_SECONDS);
    }
    LOG_INFO(
        State,
        "Will retry connection in {:.0f}s (attempt {})",
        getCurrentRetryIntervalSeconds(),
        retry_count_);

    updateStatusLabel();
    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
