#pragma once

#include "WaterSim.h"

#include <cstdint>
#include <vector>

namespace DirtSim {

class MacProjectionWaterSim final : public IWaterSim {
public:
    WaterSimMode getMode() const override { return WaterSimMode::MacProjection; }

    void reset() override;
    void resize(int worldWidth, int worldHeight) override;
    void advanceTime(World& world, double deltaTimeSeconds) override;

    bool tryGetWaterVolumeView(WaterVolumeView& out) const override;
    bool tryGetMutableWaterVolumeView(WaterVolumeMutableView& out) override;

private:
    int width_ = 0;
    int height_ = 0;

    std::vector<float> waterVolume_;
    std::vector<float> uFaceVelocity_;
    std::vector<float> vFaceVelocity_;

    std::vector<float> divergence_;
    std::vector<float> pressure_;
    std::vector<float> pressureScratch_;
    std::vector<float> hydroPressure_;
    std::vector<float> volumeScratch_;
    std::vector<uint8_t> fluidMask_;
    std::vector<uint8_t> projectionMask_;
    std::vector<uint8_t> projectionMaskScratch_;
    std::vector<uint8_t> solidMask_;
};

} // namespace DirtSim
