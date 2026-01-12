#pragma once

#include "ui/controls/IconRail.h"
#include <memory>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

namespace Api {
struct EvolutionProgress;
}

namespace Ui {

class UiComponentManager;
class EvolutionControls;
class EventSink;

/**
 * Coordinates the training view display.
 *
 * TrainingView encapsulates all LVGL widget management for the evolution
 * training UI. It creates its own dedicated display container and manages
 * progress bars, statistics labels, and control buttons.
 *
 * Similar to SimPlayground, this separates UI implementation details from
 * the state machine logic.
 */
class TrainingView {
public:
    explicit TrainingView(UiComponentManager* uiManager, EventSink& eventSink);
    ~TrainingView();

    /**
     * @brief Connect to the icon rail's selection callback.
     * Must be called after construction to enable panel switching.
     */
    void connectToIconRail();

    void updateProgress(const Api::EvolutionProgress& progress);

private:
    UiComponentManager* uiManager_;
    EventSink& eventSink_;

    lv_obj_t* averageLabel_ = nullptr;
    lv_obj_t* bestAllTimeLabel_ = nullptr;
    lv_obj_t* bestThisGenLabel_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* evalLabel_ = nullptr;
    lv_obj_t* evaluationBar_ = nullptr;
    lv_obj_t* genLabel_ = nullptr;
    lv_obj_t* generationBar_ = nullptr;

    // Panel content (created lazily).
    std::unique_ptr<EvolutionControls> evolutionControls_;

    // Currently active panel.
    IconId activePanel_ = IconId::COUNT;

    void createUI();
    void destroyUI();
    void clearPanelContent();
    void showPanelContent(IconId panelId);
    void createEvolutionPanel(lv_obj_t* container);
    void onIconSelected(IconId selectedId, IconId previousId);
};

} // namespace Ui
} // namespace DirtSim
