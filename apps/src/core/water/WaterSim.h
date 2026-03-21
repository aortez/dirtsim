#pragma once

#include "WaterSimMode.h"
#include "WaterVolumeView.h"

namespace DirtSim {

class World;
struct PhysicsSettings;

class IWaterSim {
public:
    virtual ~IWaterSim() = default;

    virtual WaterSimMode getMode() const = 0;
    virtual void reset() = 0;
    virtual void resize(int worldWidth, int worldHeight) = 0;
    virtual void advanceTime(World& world, double deltaTimeSeconds) = 0;
    virtual void syncToSettings(const PhysicsSettings& /*settings*/) {}

    virtual bool tryGetWaterVolumeView(WaterVolumeView& /*out*/) const { return false; }
    virtual bool tryGetMutableWaterVolumeView(WaterVolumeMutableView& /*out*/) { return false; }
};

} // namespace DirtSim
