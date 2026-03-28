#pragma once

#include "WaterSimMode.h"
#include "WaterVolumeView.h"

#include <cstdint>
#include <vector>

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

struct WaterSleepShadowStats {
    int blocksX = 0;
    int blocksY = 0;
    uint32_t totalWaterCells = 0;
    uint32_t totalWaterRegions = 0;
    uint32_t shadowActiveWaterRegions = 0;
    uint32_t shadowSkippableWaterCells = 0;
    uint32_t shadowSkippableWaterRegions = 0;
};

struct WaterAdvanceDebugOptions {
    bool disableGravityPreStep = false;
    bool disableGravityPreStepOnBottomInterfaceFaces = false;
    bool disableGravityPreStepOnTopInterfaceFaces = false;
    bool disableHydroPressureGradient = false;
    bool excludeAirNeighborsFromPressureDenominator = false;
    bool scaleFluidAirPressureCorrectionByCellFill = false;
    bool scaleProjectionDivergenceByCellFill = false;
    bool scaleGravityByVFaceFill = false;
    bool scaleHydroGradientByVFaceFill = false;
    bool treatFluidAirPressureBoundaryAtFace = false;
    bool useReconstructedFreeSurfaceGeometry = false;

    // Region-of-interest dump. When minX <= maxX and minY <= maxY, each phase sample
    // includes per-cell and per-face state for this bounding box.
    int regionDumpMinX = -1;
    int regionDumpMinY = -1;
    int regionDumpMaxX = -1;
    int regionDumpMaxY = -1;
};

enum class WaterAdvancePhase : uint8_t {
    BeforeGravityPreStep,
    AfterGravityPreStep,
    AfterHydroPressureGradient,
    AfterPressureProjection,
    AfterDamping,
    AfterAdvection,
    AfterResidualSettle,
    Final,
};

struct WaterAdvanceRegionCell {
    int x = 0;
    int y = 0;
    float divergence = 0.0f;
    float hydroPressure = 0.0f;
    float pressure = 0.0f;
    float volume = 0.0f;
    uint8_t fluidMask = 0;
    uint8_t projectionMask = 0;
    uint8_t solidMask = 0;
};

struct WaterAdvanceRegionFace {
    bool isUFace = false;
    int faceX = 0;
    int faceY = 0;
    float fluidAirBoundaryScale = 0.0f;
    float velocity = 0.0f;
    float weight = 0.0f;
};

struct WaterAdvanceRegionDump {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    std::vector<WaterAdvanceRegionCell> cells;
    std::vector<WaterAdvanceRegionFace> faces;
};

struct WaterAdvancePhaseSample {
    struct FluidAirBoundaryFaceSample {
        bool isUFace = false;
        bool fluidCellReconstructed = false;
        int faceX = 0;
        int faceY = 0;
        int fluidCellX = 0;
        int fluidCellY = 0;
        float alpha = 0.0f;
        float boundaryScale = 0.0f;
        float faceWeight = 0.0f;
        float fluidVolume = 0.0f;
        float normalX = 0.0f;
        float normalY = 0.0f;
    };
    struct VelocityFaceSample {
        bool isUFace = false;
        bool fluidAirBoundary = false;
        bool touchesInterfaceCell = false;
        bool touchesPartialCell = false;
        int faceX = 0;
        int faceY = 0;
        int cellAX = 0;
        int cellAY = 0;
        int cellBX = 0;
        int cellBY = 0;
        float absVelocity = 0.0f;
        float boundaryScale = 0.0f;
        float faceWeight = 0.0f;
        float hydroPressureDelta = 0.0f;
        float velocity = 0.0f;
        float volumeA = 0.0f;
        float volumeB = 0.0f;
    };

    WaterAdvancePhase phase = WaterAdvancePhase::Final;
    float avgFluidAirBoundaryScale = 0.0f;
    float absProjectedDivergence = 0.0f;
    float absProjectedDivergenceInterface = 0.0f;
    float absProjectedDivergenceBelowPartial = 0.0f;
    float absProjectedDivergenceNearFull = 0.0f;
    float absProjectedDivergencePartial = 0.0f;
    float absProjectedDivergenceSignificant = 0.0f;
    float kineticProxy = 0.0f;
    float kineticProxyNearFullFaces = 0.0f;
    float kineticProxyPartialFaces = 0.0f;
    float kineticProxySignificant = 0.0f;
    float maxFluidAirBoundaryScale = 0.0f;
    float maxAbsProjectedDivergence = 0.0f;
    float maxAbsProjectedDivergenceBelowPartial = 0.0f;
    float maxAbsProjectedDivergenceInterface = 0.0f;
    float maxAbsProjectedDivergenceNearFull = 0.0f;
    float maxAbsProjectedDivergencePartial = 0.0f;
    float maxAbsProjectedDivergenceSignificant = 0.0f;
    float maxFaceSpeed = 0.0f;
    float maxFaceSpeedSignificant = 0.0f;
    float minFluidAirBoundaryScale = 0.0f;
    float totalVolume = 0.0f;
    float surfaceHeightRange = 0.0f;
    uint32_t adjustedFluidAirBoundaryFaces = 0;
    uint32_t activeFluidAirBoundaryFaces = 0;
    uint32_t projectedBelowPartialCells = 0;
    uint32_t projectedCells = 0;
    uint32_t projectedInterfaceCells = 0;
    uint32_t projectedNearFullCells = 0;
    uint32_t projectedPartialCells = 0;
    uint32_t waterCells = 0;
    uint32_t significantWaterCells = 0;
    std::vector<FluidAirBoundaryFaceSample> fluidAirBoundarySamples;
    std::vector<VelocityFaceSample> topVelocityFaceSamples;
    WaterAdvanceRegionDump regionDump;
};

struct WaterAdvanceDiagnostics {
    int width = 0;
    int height = 0;
    std::vector<WaterAdvancePhaseSample> phaseSamples;
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
    virtual void setWaterAdvanceDiagnosticsEnabled(bool /*enabled*/) {}
    virtual void setWaterAdvanceDebugOptions(const WaterAdvanceDebugOptions& /*options*/) {}

    virtual bool tryGetWaterActivityView(WaterActivityView& /*out*/) const { return false; }
    virtual bool tryGetWaterAdvanceDiagnostics(WaterAdvanceDiagnostics& /*out*/) const
    {
        return false;
    }
    virtual bool tryGetWaterSleepShadowStats(WaterSleepShadowStats& /*out*/) const { return false; }
    virtual bool tryGetWaterVolumeView(WaterVolumeView& /*out*/) const { return false; }
    virtual bool tryGetMutableWaterVolumeView(WaterVolumeMutableView& /*out*/) { return false; }
};

} // namespace DirtSim
