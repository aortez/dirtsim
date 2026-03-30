#include "PlanPlayback.h"
#include "SearchIdle.h"
#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/PlanPlaybackPauseSet.h"
#include "server/api/PlanPlaybackStop.h"
#include "server/api/RenderFormatSet.h"
#include "ui/UiComponentManager.h"
#include "ui/UiServices.h"
#include "ui/state-machine/EventSink.h"
#include "ui/state-machine/StateMachine.h"
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {
namespace {

constexpr int kServerTimeoutMs = 2000;

void subscribeToBasicRender(StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "PlanPlayback: UI is not connected to the server");
        return;
    }

    Api::RenderFormatSet::Command command{
        .format = RenderFormat::EnumType::Basic,
        .connectionId = "",
    };
    const auto result =
        wsService.sendCommandAndGetResponse<Api::RenderFormatSet::OkayType>(command, 250);
    if (result.isError()) {
        LOG_WARN(
            State, "PlanPlayback: Failed to subscribe to render stream: {}", result.errorValue());
        return;
    }
    if (result.value().isError()) {
        LOG_WARN(
            State,
            "PlanPlayback: RenderFormatSet rejected: {}",
            result.value().errorValue().message);
    }
}

} // namespace

PlanPlayback::PlanPlayback(UUID planId) : planId_(planId)
{}

void PlanPlayback::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering PlanPlayback state");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getSimulationContainer();

    UiServices& uiServices = static_cast<UiServices&>(sm);
    EventSink& eventSink = static_cast<EventSink&>(sm);
    playground_ = std::make_unique<SimPlayground>(
        uiManager, &sm.getWebSocketService(), uiServices, eventSink, &sm.getFractalAnimator());
    DIRTSIM_ASSERT(playground_, "PlanPlayback playground creation failed");

    statusCard_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(statusCard_, 280, LV_SIZE_CONTENT);
    lv_obj_align(statusCard_, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_set_style_bg_color(statusCard_, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(statusCard_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(statusCard_, 0, 0);
    lv_obj_set_style_radius(statusCard_, 14, 0);
    lv_obj_set_style_pad_all(statusCard_, 14, 0);
    lv_obj_set_style_pad_row(statusCard_, 10, 0);
    lv_obj_set_flex_flow(statusCard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        statusCard_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(statusCard_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(statusCard_);

    auto* titleLabel = lv_label_create(statusCard_);
    lv_label_set_text(titleLabel, "Plan Playback");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_24, 0);

    bodyLabel_ = lv_label_create(statusCard_);
    lv_obj_set_width(bodyLabel_, LV_PCT(100));
    lv_label_set_long_mode(bodyLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(bodyLabel_, lv_color_hex(0xD7E2EC), 0);
    lv_obj_set_style_text_font(bodyLabel_, &lv_font_montserrat_16, 0);

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->clearContent();
        panel->hide();
        panel->resetWidth();
    }

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setAllowMinimize(false);
    iconRail->setVisible(true);
    iconRail->showIcons();
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setMinimizedAffordanceStyle(IconRail::minimizedAffordanceLeftTopSquare());
    iconRail->deselectAll();
    updateVisibleIcons(sm);

    subscribeToBasicRender(sm);
    updateBodyText();
}

void PlanPlayback::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting PlanPlayback state");
    playground_.reset();
    worldData_.reset();

    if (statusCard_ != nullptr) {
        lv_obj_del(statusCard_);
        statusCard_ = nullptr;
        bodyLabel_ = nullptr;
    }

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->setAllowMinimize(true);
        }
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
        }
    }
}

void PlanPlayback::updateVisibleIcons(StateMachine& sm)
{
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");

    std::vector<IconId> visibleIcons;
    if (paused_) {
        visibleIcons.push_back(IconId::PLAY);
    }
    else {
        visibleIcons.push_back(IconId::PAUSE);
    }
    visibleIcons.push_back(IconId::STOP);
    iconRail->setVisibleIcons(visibleIcons);
}

void PlanPlayback::updateBodyText()
{
    DIRTSIM_ASSERT(bodyLabel_, "PlanPlayback body label must exist");

    std::string text = paused_ ? "Paused." : "Running.";
    if (planId_.has_value()) {
        text += "\nPlan: ";
        text += planId_->toShortString();
    }
    if (worldData_) {
        text += "\nRendered frames: ";
        text += std::to_string(worldData_->timestep);
    }
    if (lastError_.has_value()) {
        text += "\n\nLast error:\n";
        text += lastError_.value();
    }
    lv_label_set_text(bodyLabel_, text.c_str());
}

State::Any PlanPlayback::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "PlanPlayback icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    auto* iconRail = sm.getUiComponentManager()->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");

    if (evt.selectedId == IconId::PAUSE || evt.selectedId == IconId::PLAY) {
        const bool paused = evt.selectedId == IconId::PAUSE;
        auto nextState = onEvent(
            UiApi::PlanPlaybackPauseSet::Cwc{
                UiApi::PlanPlaybackPauseSet::Command{ .paused = paused },
                nullptr,
            },
            sm);
        iconRail->deselectAll();
        return nextState;
    }

    if (evt.selectedId == IconId::STOP) {
        auto nextState = onEvent(
            UiApi::PlanPlaybackStop::Cwc{ UiApi::PlanPlaybackStop::Command{}, nullptr }, sm);
        iconRail->deselectAll();
        return nextState;
    }

    if (evt.selectedId == IconId::NONE) {
        return std::move(*this);
    }

    LOG_WARN(State, "Ignoring unsupported icon selection during PlanPlayback");
    iconRail->deselectAll();
    return std::move(*this);
}

State::Any PlanPlayback::onEvent(const PlanPlaybackStoppedReceivedEvent& evt, StateMachine& /*sm*/)
{
    if (!planId_.has_value()) {
        planId_ = evt.stopped.planId;
    }

    return SearchIdle{ std::nullopt, evt.stopped.planId };
}

State::Any PlanPlayback::onEvent(const RailModeChangedEvent& /*evt*/, StateMachine& /*sm*/)
{
    if (playground_) {
        playground_->sendDisplayResizeUpdate();
    }
    return std::move(*this);
}

State::Any PlanPlayback::onEvent(const UiUpdateEvent& evt, StateMachine& /*sm*/)
{
    if (!playground_) {
        return std::move(*this);
    }

    worldData_ = std::make_unique<WorldData>(evt.worldData);
    playground_->updateFromWorldData(*worldData_, evt.scenario_id, evt.scenario_config, 0.0);

    if (evt.scenarioVideoFrame.has_value()) {
        playground_->presentVideoFrame(evt.scenarioVideoFrame.value());
    }
    else {
        playground_->render(*worldData_, false);
    }

    updateBodyText();
    return std::move(*this);
}

State::Any PlanPlayback::onEvent(const UiApi::PlanPlaybackPauseSet::Cwc& cwc, StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        lastError_ = "UI is not connected to the server";
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(
                UiApi::PlanPlaybackPauseSet::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    Api::PlanPlaybackPauseSet::Command command{
        .paused = cwc.command.paused,
    };
    const auto result = wsService.sendCommandAndGetResponse<Api::PlanPlaybackPauseSet::OkayType>(
        command, kServerTimeoutMs);
    if (result.isError()) {
        lastError_ = result.errorValue();
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(
                UiApi::PlanPlaybackPauseSet::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }
    if (result.value().isError()) {
        lastError_ = result.value().errorValue().message;
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(
                UiApi::PlanPlaybackPauseSet::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    paused_ = result.value().value().paused;
    lastError_.reset();
    updateVisibleIcons(sm);
    updateBodyText();
    if (cwc.callback) {
        cwc.sendResponse(
            UiApi::PlanPlaybackPauseSet::Response::okay(
                UiApi::PlanPlaybackPauseSet::Okay{ .paused = paused_ }));
    }
    return std::move(*this);
}

State::Any PlanPlayback::onEvent(const UiApi::PlanPlaybackStop::Cwc& cwc, StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        lastError_ = "UI is not connected to the server";
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(
                UiApi::PlanPlaybackStop::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    Api::PlanPlaybackStop::Command command{};
    const auto result = wsService.sendCommandAndGetResponse<Api::PlanPlaybackStop::OkayType>(
        command, kServerTimeoutMs);
    if (result.isError()) {
        lastError_ = result.errorValue();
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(
                UiApi::PlanPlaybackStop::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }
    if (result.value().isError()) {
        lastError_ = result.value().errorValue().message;
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(
                UiApi::PlanPlaybackStop::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    lastError_.reset();
    if (cwc.callback) {
        cwc.sendResponse(
            UiApi::PlanPlaybackStop::Response::okay(
                UiApi::PlanPlaybackStop::Okay{ .stopped = true }));
    }
    return SearchIdle{ std::nullopt, planId_ };
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
