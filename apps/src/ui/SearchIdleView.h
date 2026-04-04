#pragma once

#include "ui/SearchModeVisuals.h"
#include "ui/controls/IconRail.h"
#include <lvgl/lvgl.h>
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {

class MazeSearchView;

class SearchIdleView {
public:
    SearchIdleView(lv_obj_t* parent, IconRail& iconRail);
    ~SearchIdleView();

    void setLastError(const std::optional<std::string>& error);
    void updateAnimations();

private:
    void createUi(lv_obj_t* parent);
    void layoutContentViewport();
    void updateErrorVisibility();

    lv_obj_t* contentRoot_ = nullptr;
    lv_obj_t* contentViewport_ = nullptr;
    lv_obj_t* errorCard_ = nullptr;
    lv_obj_t* errorLabel_ = nullptr;
    IconRail& iconRail_;
    int lastRailWidth_ = -1;
    int lastRootHeight_ = -1;
    int lastRootWidth_ = -1;
    std::optional<std::string> lastError_ = std::nullopt;
    std::unique_ptr<MazeSearchView> mazeView_;
    SearchModeVisuals searchModeVisuals_;
    lv_obj_t* titleCard_ = nullptr;
};

} // namespace Ui
} // namespace DirtSim
