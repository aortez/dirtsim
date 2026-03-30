#pragma once

#include "StateForward.h"
#include "server/api/Plan.h"
#include "ui/controls/PlanBrowserPanel.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

struct SearchPlanBrowser {
    SearchPlanBrowser(
        std::optional<Api::PlanSummary> lastSavedPlan = std::nullopt,
        std::optional<UUID> selectedPlanId = std::nullopt);

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::PlanBrowserOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanDetailOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanDetailSelect::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanPlaybackStart::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SearchStart::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "SearchPlanBrowser"; }

private:
    void applyBrowserState(const PlanBrowserState& state, StateMachine& sm);
    Result<std::monostate, std::string> startPlanPlayback(StateMachine& sm, UUID planId);
    Result<std::monostate, std::string> startSearch(StateMachine& sm);
    void updateErrorText();
    void updateVisibleIcons(StateMachine& sm);

    lv_obj_t* browserHost_ = nullptr;
    lv_obj_t* contentRoot_ = nullptr;
    lv_obj_t* errorLabel_ = nullptr;
    std::optional<std::string> lastError_ = std::nullopt;
    std::optional<Api::PlanSummary> lastSavedPlan_ = std::nullopt;
    std::unique_ptr<PlanBrowserPanel> planBrowserPanel_;
    std::optional<UUID> selectedPlanId_ = std::nullopt;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
