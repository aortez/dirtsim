#pragma once

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

namespace Api {
struct EvolutionProgress;
}

namespace Ui {

class UiComponentManager;

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
    explicit TrainingView(UiComponentManager* uiManager);
    ~TrainingView();

    void updateProgress(const Api::EvolutionProgress& progress);
    void setStopCallback(void (*callback)(lv_event_t*), void* userData);

private:
    UiComponentManager* uiManager_;

    lv_obj_t* averageLabel_ = nullptr;
    lv_obj_t* bestAllTimeLabel_ = nullptr;
    lv_obj_t* bestThisGenLabel_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* evalLabel_ = nullptr;
    lv_obj_t* evaluationBar_ = nullptr;
    lv_obj_t* genLabel_ = nullptr;
    lv_obj_t* generationBar_ = nullptr;
    lv_obj_t* stopButton_ = nullptr;

    void createUI();
    void destroyUI();
};

} // namespace Ui
} // namespace DirtSim
