#pragma once

#include "lvgl/lvgl.h"
#include "os-manager/ScannerTypes.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace DirtSim {
namespace Ui {

class ScannerChannelMapWidget {
public:
    enum class BubbleKind { Direct = 0, Incidental, Mixed };

    struct Bubble {
        int channel = 0;
        int rssiBucketDbm = -90;
        uint32_t count = 1;
        std::optional<uint64_t> freshestAgeMs;
        BubbleKind kind = BubbleKind::Direct;
    };

    struct Model {
        OsManager::ScannerBand band = OsManager::ScannerBand::Band5Ghz;
        OsManager::ScannerConfigMode mode = OsManager::ScannerConfigMode::Auto;
        std::optional<OsManager::ScannerTuning> currentTuning;
        std::vector<Bubble> bubbles;
    };

    explicit ScannerChannelMapWidget(lv_obj_t* parent);
    ~ScannerChannelMapWidget() = default;

    void clear();
    lv_obj_t* getContainer() const;
    void setModel(Model model);

private:
    static void onMapDraw(lv_event_t* e);
    void drawMap(lv_event_t* e) const;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* map_ = nullptr;
    Model model_;
};

} // namespace Ui
} // namespace DirtSim
