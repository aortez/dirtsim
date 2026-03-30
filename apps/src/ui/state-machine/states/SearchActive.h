#pragma once

#include "StateForward.h"
#include "core/WorldData.h"
#include "server/api/SearchProgress.h"
#include "ui/SimPlayground.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

struct SearchActive {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const SearchProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::SearchPauseSet::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SearchStop::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "SearchActive"; }

private:
    void updateBodyText();
    void updateVisibleIcons(StateMachine& sm);

    lv_obj_t* bodyLabel_ = nullptr;
    lv_obj_t* statusCard_ = nullptr;
    Api::SearchProgress progress_;
    std::optional<std::string> lastError_ = std::nullopt;
    std::unique_ptr<SimPlayground> playground_;
    std::unique_ptr<WorldData> worldData_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
