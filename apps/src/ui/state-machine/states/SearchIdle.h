#pragma once

#include "StateForward.h"
#include "server/api/Plan.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

struct SearchIdle {
    SearchIdle(
        std::optional<Api::PlanSummary> lastSavedPlan = std::nullopt,
        std::optional<UUID> selectedPlanId = std::nullopt,
        std::optional<std::string> lastError = std::nullopt);

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const PlanPlaybackStoppedReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::PlanBrowserOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanDetailOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanDetailSelect::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanPlaybackStart::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SearchStart::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "SearchIdle"; }

private:
    void updateVisibleIcons(StateMachine& sm);
    void updateBodyText();

    lv_obj_t* bodyLabel_ = nullptr;
    lv_obj_t* contentRoot_ = nullptr;
    std::optional<std::string> lastError_ = std::nullopt;
    std::optional<Api::PlanSummary> lastSavedPlan_ = std::nullopt;
    std::optional<UUID> selectedPlanId_ = std::nullopt;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
