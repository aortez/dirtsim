#pragma once

#include "StateForward.h"
#include "core/UUID.h"
#include "core/WorldData.h"
#include "ui/SimPlayground.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

struct PlanPlayback {
    PlanPlayback() = default;
    explicit PlanPlayback(UUID planId);

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const PlanPlaybackStoppedReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::PlanPlaybackPauseSet::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlanPlaybackStop::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "PlanPlayback"; }

private:
    void updateBodyText();
    void updateVisibleIcons(StateMachine& sm);

    lv_obj_t* bodyLabel_ = nullptr;
    lv_obj_t* statusCard_ = nullptr;
    bool paused_ = false;
    std::optional<std::string> lastError_ = std::nullopt;
    std::optional<UUID> planId_ = std::nullopt;
    std::unique_ptr<SimPlayground> playground_;
    std::unique_ptr<WorldData> worldData_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
