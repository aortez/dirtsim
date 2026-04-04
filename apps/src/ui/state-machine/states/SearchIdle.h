#pragma once

#include "StateForward.h"
#include "server/api/Plan.h"
#include "ui/SearchIdleView.h"
#include "ui/controls/SearchSettingsPanel.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/api/SearchSettingsSet.h"
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

struct SearchIdle {
    SearchIdle(
        std::optional<Api::PlanSummary> lastSavedPlan = std::nullopt,
        std::optional<UUID> selectedPlanId = std::nullopt);

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);
    void updateAnimations();

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const PlanPlaybackStoppedReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const UserSettingsUpdatedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::SearchSettingsSet::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanBrowserOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanDetailOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanDetailSelect::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanPlaybackStart::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SearchStart::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "SearchIdle"; }

private:
    void updateVisibleIcons(StateMachine& sm);

    std::optional<std::string> lastError_ = std::nullopt;
    std::optional<Api::PlanSummary> lastSavedPlan_ = std::nullopt;
    std::optional<UUID> selectedPlanId_ = std::nullopt;
    std::unique_ptr<SearchSettingsPanel> settingsPanel_;
    std::unique_ptr<SearchIdleView> view_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
