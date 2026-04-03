#pragma once

#include "ui/controls/IconRail.h"
#include "ui/rendering/maze/MazeSearchAnimator.h"
#include <memory>

namespace DirtSim {
namespace Ui {

class MazeSearchView;

class SearchModeVisuals {
public:
    SearchModeVisuals(IconRail& iconRail, int mazeWidth, int mazeHeight);
    ~SearchModeVisuals();

    MazeSearchAnimator& animator() { return animator_; }
    void updateAnimations();

private:
    MazeSearchAnimator animator_;
    std::unique_ptr<MazeSearchView> iconView_;
    IconRail& iconRail_;
};

} // namespace Ui
} // namespace DirtSim
