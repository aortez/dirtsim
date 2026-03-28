#pragma once

#include "WaterSim.h"

#include <cstdint>
#include <vector>

namespace DirtSim {

class MacProjectionWaterSim final : public IWaterSim {
public:
    struct Parameters {
        float advectionCflLimit = 0.90f;
        float advectionVolumeEpsilon = 0.0001f;
        int displacementMaxRadius = 8;
        float fluidMaskVolumeEpsilon = 0.0001f;
        int pressureIterations = 2;
        float pressureGradientVelocityScale = 1.0f;
        float velocityCflLimit = 0.95f;
        float velocityDampingPerSecond = 0.05f;
        float velocitySleepEpsilon = 0.00005f;
    };

    WaterSimMode getMode() const override { return WaterSimMode::MacProjection; }

    void reset() override;
    void resize(int worldWidth, int worldHeight) override;
    void advanceTime(World& world, double deltaTimeSeconds) override;
    void queueGuidedWaterDrain(const GuidedWaterDrain& drain) override;
    void syncToSettings(const PhysicsSettings& settings) override;

    bool tryGetWaterActivityView(WaterActivityView& out) const override;
    bool tryGetWaterAdvanceDiagnostics(WaterAdvanceDiagnostics& out) const override;
    bool tryGetWaterSleepShadowStats(WaterSleepShadowStats& out) const override;
    bool tryGetWaterVolumeView(WaterVolumeView& out) const override;
    bool tryGetMutableWaterVolumeView(WaterVolumeMutableView& out) override;
    void setWaterAdvanceDiagnosticsEnabled(bool enabled) override;
    void setWaterAdvanceDebugOptions(const WaterAdvanceDebugOptions& options) override;

    void setParametersForTesting(const Parameters& parameters) { parameters_ = parameters; }
    const Parameters& getParametersForTesting() const { return parameters_; }

private:
    void captureAdvancePhaseSample(WaterAdvancePhase phase, float invDt);
    void clearAdvanceDiagnostics();
    void rebuildWaterActivityView(const std::vector<float>& previousWaterVolume);
    void rebuildWaterSleepShadowStats(const World& world);
    void settleResidualWater();
    void applyGuidedWaterDrainOutflow(const GuidedWaterDrain& drain, float dt);
    void applyGuidedWaterDrainVelocityBias(const GuidedWaterDrain& drain);

    int width_ = 0;
    int height_ = 0;
    Parameters parameters_{};
    std::vector<GuidedWaterDrain> pendingGuidedWaterDrains_;

    std::vector<float> waterVolume_;
    std::vector<float> previousWaterVolume_;
    std::vector<float> waterActivityMaxFaceSpeed_;
    std::vector<float> waterActivityVolumeDelta_;
    std::vector<uint8_t> waterActivityFlags_;
    WaterAdvanceDiagnostics waterAdvanceDiagnostics_{};
    WaterAdvanceDebugOptions waterAdvanceDebugOptions_{};
    bool waterAdvanceDiagnosticsEnabled_ = false;
    WaterSleepShadowStats waterSleepShadowStats_{};
    std::vector<float> uFaceVelocity_;
    std::vector<float> vFaceVelocity_;

    std::vector<float> divergence_;
    std::vector<float> interfaceAlpha_;
    std::vector<float> interfaceNormalX_;
    std::vector<float> interfaceNormalY_;
    std::vector<float> pressure_;
    std::vector<float> pressureScratch_;
    std::vector<float> hydroPressure_;
    std::vector<float> uFaceFluidAirBoundaryScale_;
    std::vector<float> uFaceLiquidWeight_;
    std::vector<float> vFaceFluidAirBoundaryScale_;
    std::vector<float> vFaceLiquidWeight_;
    std::vector<float> volumeScratch_;
    std::vector<uint8_t> fluidMask_;
    std::vector<uint8_t> interfaceReconstructionMask_;
    std::vector<uint8_t> projectionMask_;
    std::vector<uint8_t> projectionMaskScratch_;
    std::vector<uint8_t> solidMask_;
};

} // namespace DirtSim
