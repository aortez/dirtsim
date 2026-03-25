#pragma once

#include "WaterSim.h"

#include <memory>

namespace DirtSim {

class World;
struct PhysicsSettings;

class WaterSimSystem {
public:
    WaterSimSystem() = default;
    ~WaterSimSystem();

    WaterSimMode getMode() const { return mode_; }

    void syncToSettings(const PhysicsSettings& settings, int worldWidth, int worldHeight);
    void advanceTime(World& world, double deltaTimeSeconds);
    void queueGuidedWaterDrain(const GuidedWaterDrain& drain);
    bool tryGetWaterActivityView(WaterActivityView& out) const;
    bool tryGetWaterSleepShadowStats(WaterSleepShadowStats& out) const;
    bool tryGetWaterVolumeView(WaterVolumeView& out) const;
    bool tryGetMutableWaterVolumeView(WaterVolumeMutableView& out);

private:
    void setMode(WaterSimMode mode, int worldWidth, int worldHeight);
    void resizeIfNeeded(int worldWidth, int worldHeight);

    WaterSimMode mode_ = WaterSimMode::LegacyCell;
    int width_ = 0;
    int height_ = 0;
    std::unique_ptr<IWaterSim> sim_;
};

} // namespace DirtSim
