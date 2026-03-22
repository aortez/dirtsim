#pragma once

#include "WaterSimMode.h"
#include "WaterVolumeView.h"

#include <cstdint>

namespace DirtSim {

class World;
struct PhysicsSettings;

struct GuidedWaterDrain {
    int16_t guideStartX = 0;
    int16_t guideEndX = -1;
    int16_t guideTopY = 0;
    int16_t guideBottomY = -1;
    int16_t mouthStartX = 0;
    int16_t mouthEndX = -1;
    int16_t mouthY = 0;
    float guideDownwardSpeed = 0.0f;
    float guideLateralSpeed = 0.0f;
    float mouthDownwardSpeed = 0.0f;
    float drainRatePerSecond = 0.0f;
};

class IWaterSim {
public:
    virtual ~IWaterSim() = default;

    virtual WaterSimMode getMode() const = 0;
    virtual void reset() = 0;
    virtual void resize(int worldWidth, int worldHeight) = 0;
    virtual void advanceTime(World& world, double deltaTimeSeconds) = 0;
    virtual void syncToSettings(const PhysicsSettings& /*settings*/) {}
    virtual void queueGuidedWaterDrain(const GuidedWaterDrain& /*drain*/) {}

    virtual bool tryGetWaterVolumeView(WaterVolumeView& /*out*/) const { return false; }
    virtual bool tryGetMutableWaterVolumeView(WaterVolumeMutableView& /*out*/) { return false; }
};

} // namespace DirtSim
