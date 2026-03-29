#include "MacProjectionWaterSim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "core/Cell.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldRegionActivityTracker.h"

namespace DirtSim {
namespace {

constexpr float kFaceWeightEpsilon = 0.0001f;
constexpr float kGeometryEpsilon = 0.000001f;
constexpr float kMinFluidAirTheta = 0.25f;

struct Interval1D {
    float min = 0.0f;
    float max = 0.0f;
    bool valid = false;
};

struct Point2 {
    float x = 0.0f;
    float y = 0.0f;
};

size_t cellIndex(int width, int x, int y)
{
    return static_cast<size_t>(y) * width + x;
}

size_t uFaceIndex(int width, int x, int y)
{
    return static_cast<size_t>(y) * (width + 1) + x;
}

size_t vFaceIndex(int width, int x, int y)
{
    return static_cast<size_t>(y) * width + x;
}

float polygonArea(const std::array<Point2, 8>& polygon, int count)
{
    if (count < 3) {
        return 0.0f;
    }

    float twiceArea = 0.0f;
    for (int i = 0; i < count; ++i) {
        const Point2& a = polygon[static_cast<size_t>(i)];
        const Point2& b = polygon[static_cast<size_t>((i + 1) % count)];
        twiceArea += (a.x * b.y) - (b.x * a.y);
    }

    return 0.5f * std::abs(twiceArea);
}

float cellCoverageAgainstHalfPlane(float normalX, float normalY, float alpha)
{
    std::array<Point2, 8> source{
        Point2{ .x = -0.5f, .y = -0.5f },
        Point2{ .x = 0.5f, .y = -0.5f },
        Point2{ .x = 0.5f, .y = 0.5f },
        Point2{ .x = -0.5f, .y = 0.5f },
    };
    std::array<Point2, 8> clipped{};
    int sourceCount = 4;
    int clippedCount = 0;

    const auto signedDistance = [&](const Point2& point) {
        return (normalX * point.x) + (normalY * point.y) - alpha;
    };

    Point2 previous = source[static_cast<size_t>(sourceCount - 1)];
    float previousDistance = signedDistance(previous);
    bool previousInside = previousDistance >= -kGeometryEpsilon;

    for (int i = 0; i < sourceCount; ++i) {
        const Point2 current = source[static_cast<size_t>(i)];
        const float currentDistance = signedDistance(current);
        const bool currentInside = currentDistance >= -kGeometryEpsilon;

        if (previousInside != currentInside) {
            const float denom = previousDistance - currentDistance;
            const float t = std::abs(denom) > kGeometryEpsilon ? (previousDistance / denom) : 0.0f;
            clipped[static_cast<size_t>(clippedCount++)] = Point2{
                .x = previous.x + (t * (current.x - previous.x)),
                .y = previous.y + (t * (current.y - previous.y)),
            };
        }

        if (currentInside) {
            clipped[static_cast<size_t>(clippedCount++)] = current;
        }

        previous = current;
        previousDistance = currentDistance;
        previousInside = currentInside;
    }

    return std::clamp(polygonArea(clipped, clippedCount), 0.0f, 1.0f);
}

Interval1D fullWetInterval()
{
    return Interval1D{
        .min = -0.5f,
        .max = 0.5f,
        .valid = true,
    };
}

float intervalLength(const Interval1D& interval)
{
    if (!interval.valid) {
        return 0.0f;
    }

    return std::max(0.0f, interval.max - interval.min);
}

float intervalOverlapLength(const Interval1D& a, const Interval1D& b)
{
    if (!a.valid || !b.valid) {
        return 0.0f;
    }

    return std::max(0.0f, std::min(a.max, b.max) - std::max(a.min, b.min));
}

Interval1D horizontalFaceWetInterval(float normalX, float normalY, float alpha, float faceY)
{
    if (std::abs(normalX) <= kGeometryEpsilon) {
        const bool isWet = (normalY * faceY) >= alpha - kGeometryEpsilon;
        return isWet ? fullWetInterval() : Interval1D{};
    }

    const float threshold = (alpha - (normalY * faceY)) / normalX;
    if (normalX > 0.0f) {
        return Interval1D{
            .min = std::clamp(threshold, -0.5f, 0.5f),
            .max = 0.5f,
            .valid = threshold < 0.5f + kGeometryEpsilon,
        };
    }

    return Interval1D{
        .min = -0.5f,
        .max = std::clamp(threshold, -0.5f, 0.5f),
        .valid = threshold > -0.5f - kGeometryEpsilon,
    };
}

Interval1D verticalFaceWetInterval(float normalX, float normalY, float alpha, float faceX)
{
    if (std::abs(normalY) <= kGeometryEpsilon) {
        const bool isWet = (normalX * faceX) >= alpha - kGeometryEpsilon;
        return isWet ? fullWetInterval() : Interval1D{};
    }

    const float threshold = (alpha - (normalX * faceX)) / normalY;
    if (normalY > 0.0f) {
        return Interval1D{
            .min = std::clamp(threshold, -0.5f, 0.5f),
            .max = 0.5f,
            .valid = threshold < 0.5f + kGeometryEpsilon,
        };
    }

    return Interval1D{
        .min = -0.5f,
        .max = std::clamp(threshold, -0.5f, 0.5f),
        .valid = threshold > -0.5f - kGeometryEpsilon,
    };
}

} // namespace

void MacProjectionWaterSim::reset()
{
    pendingGuidedWaterDrains_.clear();
    clearAdvanceDiagnostics();
    waterSleepShadowStats_ = {};
    std::fill(waterVolume_.begin(), waterVolume_.end(), 0.0f);
    std::fill(previousWaterVolume_.begin(), previousWaterVolume_.end(), 0.0f);
    std::fill(waterActivityMaxFaceSpeed_.begin(), waterActivityMaxFaceSpeed_.end(), 0.0f);
    std::fill(waterActivityVolumeDelta_.begin(), waterActivityVolumeDelta_.end(), 0.0f);
    std::fill(waterActivityFlags_.begin(), waterActivityFlags_.end(), 0);
    std::fill(uFaceVelocity_.begin(), uFaceVelocity_.end(), 0.0f);
    std::fill(vFaceVelocity_.begin(), vFaceVelocity_.end(), 0.0f);
    std::fill(divergence_.begin(), divergence_.end(), 0.0f);
    std::fill(interfaceAlpha_.begin(), interfaceAlpha_.end(), 0.0f);
    std::fill(interfaceNormalX_.begin(), interfaceNormalX_.end(), 0.0f);
    std::fill(interfaceNormalY_.begin(), interfaceNormalY_.end(), 0.0f);
    std::fill(pressure_.begin(), pressure_.end(), 0.0f);
    std::fill(pressureScratch_.begin(), pressureScratch_.end(), 0.0f);
    std::fill(hydroPressure_.begin(), hydroPressure_.end(), 0.0f);
    std::fill(uFaceFluidAirBoundaryScale_.begin(), uFaceFluidAirBoundaryScale_.end(), 1.0f);
    std::fill(uFaceHydroGradient_.begin(), uFaceHydroGradient_.end(), 0.0f);
    std::fill(uFaceLiquidWeight_.begin(), uFaceLiquidWeight_.end(), 0.0f);
    std::fill(vFaceFluidAirBoundaryScale_.begin(), vFaceFluidAirBoundaryScale_.end(), 1.0f);
    std::fill(vFaceLiquidWeight_.begin(), vFaceLiquidWeight_.end(), 0.0f);
    std::fill(volumeScratch_.begin(), volumeScratch_.end(), 0.0f);
    std::fill(fluidMask_.begin(), fluidMask_.end(), 0);
    std::fill(interfaceReconstructionMask_.begin(), interfaceReconstructionMask_.end(), 0);
    std::fill(projectionMask_.begin(), projectionMask_.end(), 0);
    std::fill(projectionMaskScratch_.begin(), projectionMaskScratch_.end(), 0);
    std::fill(solidMask_.begin(), solidMask_.end(), 0);
}

void MacProjectionWaterSim::resize(int worldWidth, int worldHeight)
{
    width_ = worldWidth;
    height_ = worldHeight;
    pendingGuidedWaterDrains_.clear();
    clearAdvanceDiagnostics();
    waterSleepShadowStats_ = {};

    const size_t cellCount = static_cast<size_t>(width_) * height_;
    const size_t uFaceCount = static_cast<size_t>(width_ + 1) * height_;
    const size_t vFaceCount = static_cast<size_t>(width_) * (height_ + 1);

    waterVolume_.assign(cellCount, 0.0f);
    previousWaterVolume_.assign(cellCount, 0.0f);
    waterActivityMaxFaceSpeed_.assign(cellCount, 0.0f);
    waterActivityVolumeDelta_.assign(cellCount, 0.0f);
    waterActivityFlags_.assign(cellCount, 0);
    uFaceVelocity_.assign(uFaceCount, 0.0f);
    vFaceVelocity_.assign(vFaceCount, 0.0f);
    divergence_.assign(cellCount, 0.0f);
    interfaceAlpha_.assign(cellCount, 0.0f);
    interfaceNormalX_.assign(cellCount, 0.0f);
    interfaceNormalY_.assign(cellCount, 0.0f);
    pressure_.assign(cellCount, 0.0f);
    pressureScratch_.assign(cellCount, 0.0f);
    hydroPressure_.assign(cellCount, 0.0f);
    uFaceFluidAirBoundaryScale_.assign(uFaceCount, 1.0f);
    uFaceHydroGradient_.assign(uFaceCount, 0.0f);
    uFaceLiquidWeight_.assign(uFaceCount, 0.0f);
    vFaceFluidAirBoundaryScale_.assign(vFaceCount, 1.0f);
    vFaceLiquidWeight_.assign(vFaceCount, 0.0f);
    volumeScratch_.assign(cellCount, 0.0f);
    fluidMask_.assign(cellCount, 0);
    interfaceReconstructionMask_.assign(cellCount, 0);
    projectionMask_.assign(cellCount, 0);
    projectionMaskScratch_.assign(cellCount, 0);
    solidMask_.assign(cellCount, 0);
}

bool MacProjectionWaterSim::tryGetWaterActivityView(WaterActivityView& out) const
{
    if (width_ <= 0 || height_ <= 0) {
        return false;
    }

    out.width = width_;
    out.height = height_;
    out.max_face_speed = waterActivityMaxFaceSpeed_;
    out.volume_delta = waterActivityVolumeDelta_;
    out.flags = waterActivityFlags_;
    return true;
}

bool MacProjectionWaterSim::tryGetWaterAdvanceDiagnostics(WaterAdvanceDiagnostics& out) const
{
    if (!waterAdvanceDiagnosticsEnabled_ || width_ <= 0 || height_ <= 0
        || waterAdvanceDiagnostics_.phaseSamples.empty()) {
        return false;
    }

    out = waterAdvanceDiagnostics_;
    return true;
}

bool MacProjectionWaterSim::tryGetWaterSleepShadowStats(WaterSleepShadowStats& out) const
{
    if (width_ <= 0 || height_ <= 0) {
        return false;
    }

    out = waterSleepShadowStats_;
    return true;
}

bool MacProjectionWaterSim::tryGetWaterVolumeView(WaterVolumeView& out) const
{
    if (width_ <= 0 || height_ <= 0) {
        return false;
    }

    out.width = width_;
    out.height = height_;
    out.volume = waterVolume_;
    return true;
}

bool MacProjectionWaterSim::tryGetMutableWaterVolumeView(WaterVolumeMutableView& out)
{
    if (width_ <= 0 || height_ <= 0) {
        return false;
    }

    out.width = width_;
    out.height = height_;
    out.volume = waterVolume_;
    return true;
}

void MacProjectionWaterSim::setWaterAdvanceDiagnosticsEnabled(bool enabled)
{
    waterAdvanceDiagnosticsEnabled_ = enabled;
    clearAdvanceDiagnostics();
}

void MacProjectionWaterSim::setWaterAdvanceDebugOptions(const WaterAdvanceDebugOptions& options)
{
    waterAdvanceDebugOptions_ = options;
}

void MacProjectionWaterSim::queueGuidedWaterDrain(const GuidedWaterDrain& drain)
{
    pendingGuidedWaterDrains_.push_back(drain);
}

void MacProjectionWaterSim::syncToSettings(const PhysicsSettings& settings)
{
    parameters_.pressureIterations = std::max(1, settings.mac_water_pressure_iterations);
    parameters_.velocityDampingPerSecond =
        std::max(0.0f, static_cast<float>(settings.mac_water_velocity_damping_per_second));
    parameters_.velocitySleepEpsilon =
        std::max(0.0f, static_cast<float>(settings.mac_water_velocity_sleep_epsilon));
}

void MacProjectionWaterSim::clearAdvanceDiagnostics()
{
    waterAdvanceDiagnostics_ = {};
    waterAdvanceDiagnostics_.width = width_;
    waterAdvanceDiagnostics_.height = height_;
}

void MacProjectionWaterSim::captureAdvancePhaseSample(WaterAdvancePhase phase, float invDt)
{
    if (!waterAdvanceDiagnosticsEnabled_ || width_ <= 0 || height_ <= 0) {
        return;
    }

    constexpr float kMinWaterVolume = 0.0001f;
    constexpr float kNearlyFullWaterVolume = 0.95f;
    constexpr float kSignificantWaterVolume = 0.05f;

    WaterAdvancePhaseSample sample{
        .phase = phase,
        .fluidAirBoundarySamples = {},
        .topVelocityFaceSamples = {},
        .regionDump = {},
    };

    auto cellFaceSpeed = [&](int x, int y) {
        return std::max(
            {
                std::abs(uFaceVelocity_[uFaceIndex(width_, x, y)]),
                std::abs(uFaceVelocity_[uFaceIndex(width_, x + 1, y)]),
                std::abs(vFaceVelocity_[vFaceIndex(width_, x, y)]),
                std::abs(vFaceVelocity_[vFaceIndex(width_, x, y + 1)]),
            });
    };

    auto cellHasFluid = [&](int x, int y) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return false;
        }

        const size_t idx = cellIndex(width_, x, y);
        return solidMask_[idx] == 0 && waterVolume_[idx] > kMinWaterVolume;
    };

    auto cellVolume = [&](int x, int y) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return 0.0f;
        }

        return std::clamp(waterVolume_[cellIndex(width_, x, y)], 0.0f, 1.0f);
    };

    auto cellIsInterface = [&](int x, int y) {
        if (!cellHasFluid(x, y)) {
            return false;
        }

        return !cellHasFluid(x - 1, y) || !cellHasFluid(x + 1, y) || !cellHasFluid(x, y - 1)
            || !cellHasFluid(x, y + 1);
    };

    auto cellDivergence = [&](int x, int y) {
        const float uRightWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
            ? uFaceLiquidWeight_[uFaceIndex(width_, x + 1, y)]
            : 1.0f;
        const float uLeftWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
            ? uFaceLiquidWeight_[uFaceIndex(width_, x, y)]
            : 1.0f;
        const float vDownWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
            ? vFaceLiquidWeight_[vFaceIndex(width_, x, y + 1)]
            : 1.0f;
        const float vUpWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
            ? vFaceLiquidWeight_[vFaceIndex(width_, x, y)]
            : 1.0f;
        const float uRight = uFaceVelocity_[uFaceIndex(width_, x + 1, y)];
        const float uLeft = uFaceVelocity_[uFaceIndex(width_, x, y)];
        const float vDown = vFaceVelocity_[vFaceIndex(width_, x, y + 1)];
        const float vUp = vFaceVelocity_[vFaceIndex(width_, x, y)];
        return ((uRightWeight * uRight) - (uLeftWeight * uLeft) + (vDownWeight * vDown)
                - (vUpWeight * vUp))
            * invDt;
    };

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] != 0) {
                continue;
            }

            const float volume = std::clamp(waterVolume_[idx], 0.0f, 1.0f);
            if (volume <= kMinWaterVolume) {
                continue;
            }

            sample.totalVolume += volume;
            sample.waterCells++;
            sample.maxFaceSpeed = std::max(sample.maxFaceSpeed, cellFaceSpeed(x, y));

            const bool isInterface = cellIsInterface(x, y);
            const bool isPartial = volume < kNearlyFullWaterVolume;
            const bool isNearFull = !isPartial;
            const float aboveVolume = cellVolume(x, y - 1);
            const bool isDirectlyBelowPartial =
                y > 0 && aboveVolume > kMinWaterVolume && aboveVolume < kNearlyFullWaterVolume;
            if (projectionMask_[idx] != 0) {
                const float absDivergence = std::abs(cellDivergence(x, y));
                sample.projectedCells++;
                sample.absProjectedDivergence += absDivergence;
                sample.maxAbsProjectedDivergence =
                    std::max(sample.maxAbsProjectedDivergence, absDivergence);
                if (isDirectlyBelowPartial) {
                    sample.projectedBelowPartialCells++;
                    sample.absProjectedDivergenceBelowPartial += absDivergence;
                    sample.maxAbsProjectedDivergenceBelowPartial =
                        std::max(sample.maxAbsProjectedDivergenceBelowPartial, absDivergence);
                }
                if (isInterface) {
                    sample.projectedInterfaceCells++;
                    sample.absProjectedDivergenceInterface += absDivergence;
                    sample.maxAbsProjectedDivergenceInterface =
                        std::max(sample.maxAbsProjectedDivergenceInterface, absDivergence);
                }
                if (isPartial) {
                    sample.projectedPartialCells++;
                    sample.absProjectedDivergencePartial += absDivergence;
                    sample.maxAbsProjectedDivergencePartial =
                        std::max(sample.maxAbsProjectedDivergencePartial, absDivergence);
                }
                if (isNearFull) {
                    sample.projectedNearFullCells++;
                    sample.absProjectedDivergenceNearFull += absDivergence;
                    sample.maxAbsProjectedDivergenceNearFull =
                        std::max(sample.maxAbsProjectedDivergenceNearFull, absDivergence);
                }
            }

            if (volume < kSignificantWaterVolume) {
                continue;
            }

            sample.significantWaterCells++;
            sample.maxFaceSpeedSignificant =
                std::max(sample.maxFaceSpeedSignificant, cellFaceSpeed(x, y));
            if (projectionMask_[idx] != 0) {
                const float absDivergence = std::abs(cellDivergence(x, y));
                sample.absProjectedDivergenceSignificant += absDivergence;
                sample.maxAbsProjectedDivergenceSignificant =
                    std::max(sample.maxAbsProjectedDivergenceSignificant, absDivergence);
            }
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 1; x < width_; ++x) {
            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                continue;
            }

            const float leftVolume = std::clamp(waterVolume_[leftIdx], 0.0f, 1.0f);
            const float rightVolume = std::clamp(waterVolume_[rightIdx], 0.0f, 1.0f);
            const float u = uFaceVelocity_[uFaceIndex(width_, x, y)];
            if (leftVolume > kMinWaterVolume || rightVolume > kMinWaterVolume) {
                sample.kineticProxy += u * u;
            }
            if (leftVolume >= kSignificantWaterVolume || rightVolume >= kSignificantWaterVolume) {
                const float uu = u * u;
                sample.kineticProxySignificant += uu;
                sample.kineticProxySignificantUFaces += uu;
                const bool leftProjected = projectionMask_[leftIdx] != 0;
                const bool rightProjected = projectionMask_[rightIdx] != 0;
                if (leftProjected != rightProjected) {
                    sample.kineticProxyFluidAirBoundaryFaces += uu;
                }
                const bool leftPartial =
                    leftVolume > kMinWaterVolume && leftVolume < kNearlyFullWaterVolume;
                const bool rightPartial =
                    rightVolume > kMinWaterVolume && rightVolume < kNearlyFullWaterVolume;
                if (leftPartial || rightPartial) {
                    sample.kineticProxyPartialFaces += uu;
                }
                else {
                    sample.kineticProxyNearFullFaces += uu;
                }
            }
        }
    }

    for (int y = 1; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                continue;
            }

            const float topVolume = std::clamp(waterVolume_[topIdx], 0.0f, 1.0f);
            const float bottomVolume = std::clamp(waterVolume_[bottomIdx], 0.0f, 1.0f);
            const float v = vFaceVelocity_[vFaceIndex(width_, x, y)];
            if (topVolume > kMinWaterVolume || bottomVolume > kMinWaterVolume) {
                sample.kineticProxy += v * v;
            }
            if (topVolume >= kSignificantWaterVolume || bottomVolume >= kSignificantWaterVolume) {
                const float vv = v * v;
                sample.kineticProxySignificant += vv;
                sample.kineticProxySignificantVFaces += vv;
                const bool topProjected = projectionMask_[topIdx] != 0;
                const bool bottomProjected = projectionMask_[bottomIdx] != 0;
                if (topProjected != bottomProjected) {
                    sample.kineticProxyFluidAirBoundaryFaces += vv;
                }
                const bool topPartial =
                    topVolume > kMinWaterVolume && topVolume < kNearlyFullWaterVolume;
                const bool bottomPartial =
                    bottomVolume > kMinWaterVolume && bottomVolume < kNearlyFullWaterVolume;
                if (topPartial || bottomPartial) {
                    sample.kineticProxyPartialFaces += vv;
                }
                else {
                    sample.kineticProxyNearFullFaces += vv;
                }
            }
        }
    }

    const float defaultFluidAirBoundaryScale =
        waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
        ? 2.0f
        : (waterAdvanceDebugOptions_.treatFluidAirPressureBoundaryAtFace ? 2.0f : 1.0f);
    float fluidAirBoundaryScaleSum = 0.0f;
    sample.minFluidAirBoundaryScale = std::numeric_limits<float>::max();

    const auto recordFluidAirBoundaryScale = [&](float scale) {
        sample.activeFluidAirBoundaryFaces++;
        fluidAirBoundaryScaleSum += scale;
        sample.minFluidAirBoundaryScale = std::min(sample.minFluidAirBoundaryScale, scale);
        sample.maxFluidAirBoundaryScale = std::max(sample.maxFluidAirBoundaryScale, scale);
        if (std::abs(scale - defaultFluidAirBoundaryScale) > 0.001f) {
            sample.adjustedFluidAirBoundaryFaces++;
        }
    };
    const auto maybeRecordFluidAirBoundaryFaceSample = [&](bool isUFace,
                                                           int faceX,
                                                           int faceY,
                                                           int fluidCellX,
                                                           int fluidCellY,
                                                           float faceWeight,
                                                           float boundaryScale) {
        if (sample.fluidAirBoundarySamples.size() >= 8) {
            return;
        }

        const size_t fluidIdx = cellIndex(width_, fluidCellX, fluidCellY);
        sample.fluidAirBoundarySamples.push_back(
            WaterAdvancePhaseSample::FluidAirBoundaryFaceSample{
                .isUFace = isUFace,
                .fluidCellReconstructed = interfaceReconstructionMask_[fluidIdx] != 0,
                .faceX = faceX,
                .faceY = faceY,
                .fluidCellX = fluidCellX,
                .fluidCellY = fluidCellY,
                .alpha = interfaceAlpha_[fluidIdx],
                .boundaryScale = boundaryScale,
                .faceWeight = faceWeight,
                .fluidVolume = std::clamp(waterVolume_[fluidIdx], 0.0f, 1.0f),
                .normalX = interfaceNormalX_[fluidIdx],
                .normalY = interfaceNormalY_[fluidIdx],
            });
    };
    const auto maybeRecordTopVelocityFaceSample =
        [&](const WaterAdvancePhaseSample::VelocityFaceSample& candidate) {
            if (candidate.absVelocity <= 0.0f) {
                return;
            }

            auto insertIt = sample.topVelocityFaceSamples.end();
            for (auto it = sample.topVelocityFaceSamples.begin();
                 it != sample.topVelocityFaceSamples.end();
                 ++it) {
                if (candidate.absVelocity > it->absVelocity) {
                    insertIt = it;
                    break;
                }
            }

            if (insertIt == sample.topVelocityFaceSamples.end()) {
                if (sample.topVelocityFaceSamples.size() >= 6) {
                    return;
                }
                sample.topVelocityFaceSamples.push_back(candidate);
                return;
            }

            sample.topVelocityFaceSamples.insert(insertIt, candidate);
            if (sample.topVelocityFaceSamples.size() > 6) {
                sample.topVelocityFaceSamples.pop_back();
            }
        };

    for (int y = 0; y < height_; ++y) {
        for (int x = 1; x < width_; ++x) {
            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                continue;
            }
            if ((projectionMask_[leftIdx] != 0) == (projectionMask_[rightIdx] != 0)) {
                continue;
            }

            const size_t faceIdx = uFaceIndex(width_, x, y);
            const float faceWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                ? uFaceLiquidWeight_[faceIdx]
                : 1.0f;
            if (faceWeight <= kFaceWeightEpsilon) {
                continue;
            }

            const float scale = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                ? uFaceFluidAirBoundaryScale_[faceIdx]
                : defaultFluidAirBoundaryScale;
            recordFluidAirBoundaryScale(scale);
            const bool leftProjected = projectionMask_[leftIdx] != 0;
            maybeRecordFluidAirBoundaryFaceSample(
                true, x, y, leftProjected ? x - 1 : x, y, faceWeight, scale);
        }
    }

    for (int y = 1; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                continue;
            }
            if ((projectionMask_[topIdx] != 0) == (projectionMask_[bottomIdx] != 0)) {
                continue;
            }

            const size_t faceIdx = vFaceIndex(width_, x, y);
            const float faceWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                ? vFaceLiquidWeight_[faceIdx]
                : 1.0f;
            if (faceWeight <= kFaceWeightEpsilon) {
                continue;
            }

            const float scale = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                ? vFaceFluidAirBoundaryScale_[faceIdx]
                : defaultFluidAirBoundaryScale;
            recordFluidAirBoundaryScale(scale);
            const bool topProjected = projectionMask_[topIdx] != 0;
            maybeRecordFluidAirBoundaryFaceSample(
                false, x, y, x, topProjected ? y - 1 : y, faceWeight, scale);
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 1; x < width_; ++x) {
            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                continue;
            }

            const float leftVolume = std::clamp(waterVolume_[leftIdx], 0.0f, 1.0f);
            const float rightVolume = std::clamp(waterVolume_[rightIdx], 0.0f, 1.0f);
            if (leftVolume < kSignificantWaterVolume && rightVolume < kSignificantWaterVolume) {
                continue;
            }

            const size_t faceIdx = uFaceIndex(width_, x, y);
            maybeRecordTopVelocityFaceSample(
                WaterAdvancePhaseSample::VelocityFaceSample{
                    .isUFace = true,
                    .fluidAirBoundary =
                        (projectionMask_[leftIdx] != 0) != (projectionMask_[rightIdx] != 0),
                    .touchesInterfaceCell = cellIsInterface(x - 1, y) || cellIsInterface(x, y),
                    .touchesPartialCell =
                        (leftVolume > kMinWaterVolume && leftVolume < kNearlyFullWaterVolume)
                        || (rightVolume > kMinWaterVolume && rightVolume < kNearlyFullWaterVolume),
                    .faceX = x,
                    .faceY = y,
                    .cellAX = x - 1,
                    .cellAY = y,
                    .cellBX = x,
                    .cellBY = y,
                    .absVelocity = std::abs(uFaceVelocity_[faceIdx]),
                    .boundaryScale = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                        ? uFaceFluidAirBoundaryScale_[faceIdx]
                        : defaultFluidAirBoundaryScale,
                    .faceWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                        ? uFaceLiquidWeight_[faceIdx]
                        : 1.0f,
                    .hydroPressureDelta = hydroPressure_[rightIdx] - hydroPressure_[leftIdx],
                    .velocity = uFaceVelocity_[faceIdx],
                    .volumeA = leftVolume,
                    .volumeB = rightVolume,
                });
        }
    }

    for (int y = 1; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                continue;
            }

            const float topVolume = std::clamp(waterVolume_[topIdx], 0.0f, 1.0f);
            const float bottomVolume = std::clamp(waterVolume_[bottomIdx], 0.0f, 1.0f);
            if (topVolume < kSignificantWaterVolume && bottomVolume < kSignificantWaterVolume) {
                continue;
            }

            const size_t faceIdx = vFaceIndex(width_, x, y);
            maybeRecordTopVelocityFaceSample(
                WaterAdvancePhaseSample::VelocityFaceSample{
                    .isUFace = false,
                    .fluidAirBoundary =
                        (projectionMask_[topIdx] != 0) != (projectionMask_[bottomIdx] != 0),
                    .touchesInterfaceCell = cellIsInterface(x, y - 1) || cellIsInterface(x, y),
                    .touchesPartialCell =
                        (topVolume > kMinWaterVolume && topVolume < kNearlyFullWaterVolume)
                        || (bottomVolume > kMinWaterVolume
                            && bottomVolume < kNearlyFullWaterVolume),
                    .faceX = x,
                    .faceY = y,
                    .cellAX = x,
                    .cellAY = y - 1,
                    .cellBX = x,
                    .cellBY = y,
                    .absVelocity = std::abs(vFaceVelocity_[faceIdx]),
                    .boundaryScale = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                        ? vFaceFluidAirBoundaryScale_[faceIdx]
                        : defaultFluidAirBoundaryScale,
                    .faceWeight = waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry
                        ? vFaceLiquidWeight_[faceIdx]
                        : 1.0f,
                    .hydroPressureDelta = hydroPressure_[bottomIdx] - hydroPressure_[topIdx],
                    .velocity = vFaceVelocity_[faceIdx],
                    .volumeA = topVolume,
                    .volumeB = bottomVolume,
                });
        }
    }

    if (sample.activeFluidAirBoundaryFaces > 0) {
        sample.avgFluidAirBoundaryScale =
            fluidAirBoundaryScaleSum / static_cast<float>(sample.activeFluidAirBoundaryFaces);
    }
    else {
        sample.minFluidAirBoundaryScale = 0.0f;
    }

    int minSurfaceY = height_;
    int maxSurfaceY = -1;
    for (int x = 0; x < width_; ++x) {
        for (int y = 0; y < height_; ++y) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] != 0 || waterVolume_[idx] < kSignificantWaterVolume) {
                continue;
            }

            minSurfaceY = std::min(minSurfaceY, y);
            maxSurfaceY = std::max(maxSurfaceY, y);
            break;
        }
    }

    if (maxSurfaceY >= minSurfaceY) {
        sample.surfaceHeightRange = static_cast<float>(maxSurfaceY - minSurfaceY);
    }

    // Region-of-interest dump.
    const auto& roi = waterAdvanceDebugOptions_;
    if (roi.regionDumpMinX <= roi.regionDumpMaxX && roi.regionDumpMinY <= roi.regionDumpMaxY) {
        const int rMinX = std::max(0, roi.regionDumpMinX);
        const int rMinY = std::max(0, roi.regionDumpMinY);
        const int rMaxX = std::min(width_ - 1, roi.regionDumpMaxX);
        const int rMaxY = std::min(height_ - 1, roi.regionDumpMaxY);

        WaterAdvanceRegionDump& dump = sample.regionDump;
        dump.minX = rMinX;
        dump.minY = rMinY;
        dump.maxX = rMaxX;
        dump.maxY = rMaxY;

        for (int y = rMinY; y <= rMaxY; ++y) {
            for (int x = rMinX; x <= rMaxX; ++x) {
                const size_t idx = cellIndex(width_, x, y);
                dump.cells.push_back(
                    WaterAdvanceRegionCell{
                        .x = x,
                        .y = y,
                        .divergence = divergence_[idx],
                        .hydroPressure = hydroPressure_[idx],
                        .pressure = pressure_[idx],
                        .volume = std::clamp(waterVolume_[idx], 0.0f, 1.0f),
                        .fluidMask = fluidMask_[idx],
                        .projectionMask = projectionMask_[idx],
                        .solidMask = solidMask_[idx],
                    });
            }
        }

        // U-faces: x from rMinX to rMaxX+1, y from rMinY to rMaxY.
        for (int y = rMinY; y <= rMaxY; ++y) {
            for (int x = rMinX; x <= std::min(width_, rMaxX + 1); ++x) {
                const size_t faceIdx = uFaceIndex(width_, x, y);
                dump.faces.push_back(
                    WaterAdvanceRegionFace{
                        .isUFace = true,
                        .faceX = x,
                        .faceY = y,
                        .fluidAirBoundaryScale = uFaceFluidAirBoundaryScale_[faceIdx],
                        .velocity = uFaceVelocity_[faceIdx],
                        .weight = uFaceLiquidWeight_[faceIdx],
                    });
            }
        }

        // V-faces: x from rMinX to rMaxX, y from rMinY to rMaxY+1.
        for (int y = rMinY; y <= std::min(height_, rMaxY + 1); ++y) {
            for (int x = rMinX; x <= rMaxX; ++x) {
                const size_t faceIdx = vFaceIndex(width_, x, y);
                dump.faces.push_back(
                    WaterAdvanceRegionFace{
                        .isUFace = false,
                        .faceX = x,
                        .faceY = y,
                        .fluidAirBoundaryScale = vFaceFluidAirBoundaryScale_[faceIdx],
                        .velocity = vFaceVelocity_[faceIdx],
                        .weight = vFaceLiquidWeight_[faceIdx],
                    });
            }
        }
    }

    waterAdvanceDiagnostics_.width = width_;
    waterAdvanceDiagnostics_.height = height_;
    waterAdvanceDiagnostics_.phaseSamples.push_back(sample);
}

void MacProjectionWaterSim::advanceTime(World& world, double deltaTimeSeconds)
{
    if (width_ <= 0 || height_ <= 0) {
        return;
    }
    clearAdvanceDiagnostics();
    const WorldData& data = world.getData();

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            const Cell& cell = data.at(x, y);
            const bool solid = !cell.isEmpty() && cell.material_type != Material::EnumType::Air;
            solidMask_[idx] = solid ? 1 : 0;
        }
    }

    std::copy(waterVolume_.begin(), waterVolume_.end(), previousWaterVolume_.begin());
    rebuildWaterSleepShadowStats(world);
    if (deltaTimeSeconds <= 0.0) {
        rebuildWaterActivityView(previousWaterVolume_);
        return;
    }

    const float dt = static_cast<float>(deltaTimeSeconds);
    const float invDt = 1.0f / dt;
    const float gravity = static_cast<float>(world.getPhysicsSettings().gravity);
    const Parameters& parameters = parameters_;

    float totalWaterVolume = 0.0f;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] == 0) {
                totalWaterVolume += waterVolume_[idx];
                continue;
            }

            float remaining = waterVolume_[idx];
            if (remaining <= 0.0f) {
                continue;
            }

            waterVolume_[idx] = 0.0f;

            const auto tryDisplace = [&](int nx, int ny) {
                if (remaining <= 0.0f) {
                    return;
                }
                if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) {
                    return;
                }

                const size_t nIdx = cellIndex(width_, nx, ny);
                if (solidMask_[nIdx] != 0) {
                    return;
                }

                const float capacity = std::max(0.0f, 1.0f - waterVolume_[nIdx]);
                if (capacity <= 0.0f) {
                    return;
                }

                const float transfer = std::min(capacity, remaining);
                waterVolume_[nIdx] += transfer;
                remaining -= transfer;
            };

            tryDisplace(x - 1, y);
            tryDisplace(x + 1, y);
            tryDisplace(x, y - 1);
            tryDisplace(x, y + 1);

            for (int radius = 2; radius <= parameters.displacementMaxRadius && remaining > 0.0f;
                 ++radius) {
                const int xMin = x - radius;
                const int xMax = x + radius;
                const int yMin = y - radius;
                const int yMax = y + radius;

                for (int nx = xMin; nx <= xMax; ++nx) {
                    tryDisplace(nx, yMin);
                    tryDisplace(nx, yMax);
                }

                for (int ny = yMin + 1; ny <= yMax - 1; ++ny) {
                    tryDisplace(xMin, ny);
                    tryDisplace(xMax, ny);
                }
            }
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            fluidMask_[idx] =
                (solidMask_[idx] == 0 && waterVolume_[idx] > parameters.fluidMaskVolumeEpsilon) ? 1
                                                                                                : 0;
        }
    }

    for (int x = 0; x < width_; ++x) {
        float depthToBottom = 0.0f;
        for (int y = 0; y < height_; ++y) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] != 0 || fluidMask_[idx] == 0) {
                depthToBottom = 0.0f;
                hydroPressure_[idx] = 0.0f;
                continue;
            }

            const float cellVolume = waterVolume_[idx];
            depthToBottom += cellVolume;
            hydroPressure_[idx] = gravity * depthToBottom;
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] != 0) {
                projectionMask_[idx] = 0;
                continue;
            }

            projectionMask_[idx] = fluidMask_[idx] != 0 ? 1 : 0;
        }
    }

    std::fill(interfaceAlpha_.begin(), interfaceAlpha_.end(), 0.0f);
    std::fill(interfaceNormalX_.begin(), interfaceNormalX_.end(), 0.0f);
    std::fill(interfaceNormalY_.begin(), interfaceNormalY_.end(), 0.0f);
    std::fill(interfaceReconstructionMask_.begin(), interfaceReconstructionMask_.end(), 0);
    std::fill(uFaceFluidAirBoundaryScale_.begin(), uFaceFluidAirBoundaryScale_.end(), 1.0f);
    std::fill(uFaceLiquidWeight_.begin(), uFaceLiquidWeight_.end(), 0.0f);
    std::fill(vFaceFluidAirBoundaryScale_.begin(), vFaceFluidAirBoundaryScale_.end(), 1.0f);
    std::fill(vFaceLiquidWeight_.begin(), vFaceLiquidWeight_.end(), 0.0f);

    if (waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry) {
        const float reconstructionVolumeEpsilon = parameters.fluidMaskVolumeEpsilon;
        const float reconstructionFullThreshold = 1.0f - reconstructionVolumeEpsilon;

        const auto fluidFractionAt = [&](int sampleX, int sampleY, float fallback) {
            if (sampleX < 0 || sampleX >= width_ || sampleY < 0 || sampleY >= height_) {
                return fallback;
            }

            const size_t sampleIdx = cellIndex(width_, sampleX, sampleY);
            if (solidMask_[sampleIdx] != 0) {
                return fallback;
            }

            return std::clamp(waterVolume_[sampleIdx], 0.0f, 1.0f);
        };

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t idx = cellIndex(width_, x, y);
                if (solidMask_[idx] != 0) {
                    continue;
                }

                const float currentVolume = std::clamp(waterVolume_[idx], 0.0f, 1.0f);
                if (currentVolume <= reconstructionVolumeEpsilon
                    || currentVolume >= reconstructionFullThreshold) {
                    continue;
                }

                float normalX = 0.5f
                    * (fluidFractionAt(x + 1, y, currentVolume)
                       - fluidFractionAt(x - 1, y, currentVolume));
                float normalY = 0.5f
                    * (fluidFractionAt(x, y + 1, currentVolume)
                       - fluidFractionAt(x, y - 1, currentVolume));
                float normalLength = std::sqrt(normalX * normalX + normalY * normalY);

                if (normalLength < reconstructionVolumeEpsilon) {
                    const float deficitLeft =
                        currentVolume - fluidFractionAt(x - 1, y, currentVolume);
                    const float deficitRight =
                        currentVolume - fluidFractionAt(x + 1, y, currentVolume);
                    const float deficitUp =
                        currentVolume - fluidFractionAt(x, y - 1, currentVolume);
                    const float deficitDown =
                        currentVolume - fluidFractionAt(x, y + 1, currentVolume);

                    float bestDeficit = deficitUp;
                    normalX = 0.0f;
                    normalY = 1.0f;
                    if (deficitDown > bestDeficit) {
                        bestDeficit = deficitDown;
                        normalX = 0.0f;
                        normalY = -1.0f;
                    }
                    if (deficitLeft > bestDeficit) {
                        bestDeficit = deficitLeft;
                        normalX = 1.0f;
                        normalY = 0.0f;
                    }
                    if (deficitRight > bestDeficit) {
                        normalX = -1.0f;
                        normalY = 0.0f;
                    }
                }
                else {
                    normalX /= normalLength;
                    normalY /= normalLength;
                }

                const float alphaExtent = 0.5f * (std::abs(normalX) + std::abs(normalY));
                float alphaLo = -alphaExtent;
                float alphaHi = alphaExtent;
                for (int iteration = 0; iteration < 16; ++iteration) {
                    const float alphaMid = 0.5f * (alphaLo + alphaHi);
                    const float coverage = cellCoverageAgainstHalfPlane(normalX, normalY, alphaMid);
                    if (coverage > currentVolume) {
                        alphaLo = alphaMid;
                    }
                    else {
                        alphaHi = alphaMid;
                    }
                }

                interfaceNormalX_[idx] = normalX;
                interfaceNormalY_[idx] = normalY;
                interfaceAlpha_[idx] = 0.5f * (alphaLo + alphaHi);
                interfaceReconstructionMask_[idx] = 1;
            }
        }

        const auto uFaceIntervalForCell = [&](size_t idx, bool cellHasFluid, float faceX) {
            if (!cellHasFluid || solidMask_[idx] != 0) {
                return Interval1D{};
            }

            const float volume = std::clamp(waterVolume_[idx], 0.0f, 1.0f);
            if (volume <= reconstructionVolumeEpsilon) {
                return Interval1D{};
            }
            if (volume >= reconstructionFullThreshold || interfaceReconstructionMask_[idx] == 0) {
                return fullWetInterval();
            }

            return verticalFaceWetInterval(
                interfaceNormalX_[idx], interfaceNormalY_[idx], interfaceAlpha_[idx], faceX);
        };
        const auto vFaceIntervalForCell = [&](size_t idx, bool cellHasFluid, float faceY) {
            if (!cellHasFluid || solidMask_[idx] != 0) {
                return Interval1D{};
            }

            const float volume = std::clamp(waterVolume_[idx], 0.0f, 1.0f);
            if (volume <= reconstructionVolumeEpsilon) {
                return Interval1D{};
            }
            if (volume >= reconstructionFullThreshold || interfaceReconstructionMask_[idx] == 0) {
                return fullWetInterval();
            }

            return horizontalFaceWetInterval(
                interfaceNormalX_[idx], interfaceNormalY_[idx], interfaceAlpha_[idx], faceY);
        };
        const auto uFaceFluidAirThetaForCell =
            [&](size_t idx, const Interval1D& interval, bool airOnRight) {
                if (interfaceReconstructionMask_[idx] == 0 || !interval.valid) {
                    return 0.5f;
                }

                const float normalX = interfaceNormalX_[idx];
                if (std::abs(normalX) <= kGeometryEpsilon) {
                    return 0.5f;
                }

                const float normalY = interfaceNormalY_[idx];
                const float intervalMid = 0.5f * (interval.min + interval.max);
                const float xInterface = (interfaceAlpha_[idx] - (normalY * intervalMid)) / normalX;
                const float theta = airOnRight ? xInterface : -xInterface;
                if (theta <= kGeometryEpsilon || theta >= 0.5f - kGeometryEpsilon) {
                    return 0.5f;
                }

                return std::clamp(theta, kMinFluidAirTheta, 0.5f);
            };
        const auto vFaceFluidAirThetaForCell =
            [&](size_t idx, const Interval1D& interval, bool airBelow) {
                if (interfaceReconstructionMask_[idx] == 0 || !interval.valid) {
                    return 0.5f;
                }

                const float normalY = interfaceNormalY_[idx];
                if (std::abs(normalY) <= kGeometryEpsilon) {
                    return 0.5f;
                }

                const float normalX = interfaceNormalX_[idx];
                const float intervalMid = 0.5f * (interval.min + interval.max);
                const float yInterface = (interfaceAlpha_[idx] - (normalX * intervalMid)) / normalY;
                const float theta = airBelow ? yInterface : -yInterface;
                if (theta <= kGeometryEpsilon || theta >= 0.5f - kGeometryEpsilon) {
                    return 0.5f;
                }

                return std::clamp(theta, kMinFluidAirTheta, 0.5f);
            };

        for (int y = 0; y < height_; ++y) {
            for (int x = 1; x < width_; ++x) {
                const size_t faceIdx = uFaceIndex(width_, x, y);
                const size_t leftIdx = cellIndex(width_, x - 1, y);
                const size_t rightIdx = cellIndex(width_, x, y);
                if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                    continue;
                }

                const bool leftFluid = fluidMask_[leftIdx] != 0;
                const bool rightFluid = fluidMask_[rightIdx] != 0;
                if (!leftFluid && !rightFluid) {
                    continue;
                }

                const Interval1D leftInterval = uFaceIntervalForCell(leftIdx, leftFluid, 0.5f);
                const Interval1D rightInterval = uFaceIntervalForCell(rightIdx, rightFluid, -0.5f);

                uFaceLiquidWeight_[faceIdx] = leftFluid && rightFluid
                    ? intervalOverlapLength(leftInterval, rightInterval)
                    : intervalLength(leftFluid ? leftInterval : rightInterval);
                if (leftFluid != rightFluid) {
                    const bool airOnRight = leftFluid;
                    const size_t fluidIdx = leftFluid ? leftIdx : rightIdx;
                    const Interval1D fluidInterval = leftFluid ? leftInterval : rightInterval;
                    const float theta =
                        uFaceFluidAirThetaForCell(fluidIdx, fluidInterval, airOnRight);
                    uFaceFluidAirBoundaryScale_[faceIdx] = 1.0f / theta;
                }
            }
        }

        for (int y = 1; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t faceIdx = vFaceIndex(width_, x, y);
                const size_t topIdx = cellIndex(width_, x, y - 1);
                const size_t bottomIdx = cellIndex(width_, x, y);
                if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                    continue;
                }

                const bool topFluid = fluidMask_[topIdx] != 0;
                const bool bottomFluid = fluidMask_[bottomIdx] != 0;
                if (!topFluid && !bottomFluid) {
                    continue;
                }

                const Interval1D topInterval = vFaceIntervalForCell(topIdx, topFluid, 0.5f);
                const Interval1D bottomInterval =
                    vFaceIntervalForCell(bottomIdx, bottomFluid, -0.5f);

                vFaceLiquidWeight_[faceIdx] = topFluid && bottomFluid
                    ? intervalOverlapLength(topInterval, bottomInterval)
                    : intervalLength(topFluid ? topInterval : bottomInterval);
                if (topFluid != bottomFluid) {
                    const bool airBelow = topFluid;
                    const size_t fluidIdx = topFluid ? topIdx : bottomIdx;
                    const Interval1D fluidInterval = topFluid ? topInterval : bottomInterval;
                    const float theta =
                        vFaceFluidAirThetaForCell(fluidIdx, fluidInterval, airBelow);
                    vFaceFluidAirBoundaryScale_[faceIdx] = 1.0f / theta;
                }
            }
        }

        // Geometry-aware u-face hydro gradient. Computes the hydrostatic pressure
        // at each u-face using the PLIC liquid height at the face position, instead
        // of the column-averaged cell volume used by the default hydro assembly.
        if (waterAdvanceDebugOptions_.useReconstructedHydroAssembly) {
            for (int x = 1; x < width_; ++x) {
                float cumFromLeftCol = 0.0f;
                float cumFromRightCol = 0.0f;
                for (int y = 0; y < height_; ++y) {
                    const size_t leftIdx = cellIndex(width_, x - 1, y);
                    const size_t rightIdx = cellIndex(width_, x, y);

                    // Left column: fluid height at the face (right edge, local x = +0.5).
                    if (solidMask_[leftIdx] != 0 || fluidMask_[leftIdx] == 0) {
                        cumFromLeftCol = 0.0f;
                    }
                    else {
                        const Interval1D interval = uFaceIntervalForCell(leftIdx, true, 0.5f);
                        cumFromLeftCol += intervalLength(interval);
                    }

                    // Right column: fluid height at the face (left edge, local x = -0.5).
                    if (solidMask_[rightIdx] != 0 || fluidMask_[rightIdx] == 0) {
                        cumFromRightCol = 0.0f;
                    }
                    else {
                        const Interval1D interval = uFaceIntervalForCell(rightIdx, true, -0.5f);
                        cumFromRightCol += intervalLength(interval);
                    }

                    const size_t faceIdx = uFaceIndex(width_, x, y);
                    uFaceHydroGradient_[faceIdx] = gravity * (cumFromRightCol - cumFromLeftCol);
                }
            }
        }
    }

    if (totalWaterVolume <= 0.0f) {
        pendingGuidedWaterDrains_.clear();
        std::fill(uFaceVelocity_.begin(), uFaceVelocity_.end(), 0.0f);
        std::fill(vFaceVelocity_.begin(), vFaceVelocity_.end(), 0.0f);
        captureAdvancePhaseSample(WaterAdvancePhase::Final, invDt);
        rebuildWaterActivityView(previousWaterVolume_);
        return;
    }

    const bool useReconstructedFreeSurfaceGeometry =
        waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry;
    const float legacyFluidAirBoundaryScale =
        waterAdvanceDebugOptions_.treatFluidAirPressureBoundaryAtFace ? 2.0f : 1.0f;
    const auto uFaceWeight = [&](int x, int y) {
        if (x <= 0 || x >= width_) {
            return 0.0f;
        }

        if (!useReconstructedFreeSurfaceGeometry) {
            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            return (solidMask_[leftIdx] == 0 && solidMask_[rightIdx] == 0
                    && (fluidMask_[leftIdx] != 0 || fluidMask_[rightIdx] != 0))
                ? 1.0f
                : 0.0f;
        }

        return uFaceLiquidWeight_[uFaceIndex(width_, x, y)];
    };
    const auto uFaceFluidAirBoundaryScale = [&](int x, int y) {
        if (x <= 0 || x >= width_) {
            return legacyFluidAirBoundaryScale;
        }

        return useReconstructedFreeSurfaceGeometry
            ? uFaceFluidAirBoundaryScale_[uFaceIndex(width_, x, y)]
            : legacyFluidAirBoundaryScale;
    };
    const auto vFaceWeight = [&](int x, int y) {
        if (y <= 0 || y >= height_) {
            return 0.0f;
        }

        if (!useReconstructedFreeSurfaceGeometry) {
            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            return (solidMask_[topIdx] == 0 && solidMask_[bottomIdx] == 0
                    && (fluidMask_[topIdx] != 0 || fluidMask_[bottomIdx] != 0))
                ? 1.0f
                : 0.0f;
        }

        return vFaceLiquidWeight_[vFaceIndex(width_, x, y)];
    };
    const auto vFaceFluidAirBoundaryScale = [&](int x, int y) {
        if (y <= 0 || y >= height_) {
            return legacyFluidAirBoundaryScale;
        }

        return useReconstructedFreeSurfaceGeometry
            ? vFaceFluidAirBoundaryScale_[vFaceIndex(width_, x, y)]
            : legacyFluidAirBoundaryScale;
    };

    // Frozen-state diagnostic: save volumes before forces and optionally zero velocities.
    const bool freezeVolume = waterAdvanceDebugOptions_.freezeWaterVolume;
    std::vector<float> frozenVolume;
    if (freezeVolume) {
        frozenVolume = waterVolume_;
    }

    if (waterAdvanceDebugOptions_.zeroVelocitiesBeforeForces) {
        std::fill(uFaceVelocity_.begin(), uFaceVelocity_.end(), 0.0f);
        std::fill(vFaceVelocity_.begin(), vFaceVelocity_.end(), 0.0f);
    }

    captureAdvancePhaseSample(WaterAdvancePhase::BeforeGravityPreStep, invDt);

    for (int y = 0; y <= height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t faceIdx = vFaceIndex(width_, x, y);
            if (y == 0 || y == height_) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            if (vFaceWeight(x, y) <= kFaceWeightEpsilon) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const bool topHasFluid = fluidMask_[topIdx] != 0;
            const bool bottomHasFluid = fluidMask_[bottomIdx] != 0;
            const bool skipGravityPreStepForFace = waterAdvanceDebugOptions_.disableGravityPreStep
                || (waterAdvanceDebugOptions_.disableGravityPreStepOnTopInterfaceFaces
                    && !topHasFluid && bottomHasFluid)
                || (waterAdvanceDebugOptions_.disableGravityPreStepOnBottomInterfaceFaces
                    && topHasFluid && !bottomHasFluid);
            if (!skipGravityPreStepForFace) {
                float gravityScale = 1.0f;
                if (waterAdvanceDebugOptions_.scaleGravityByVFaceFill) {
                    // The hydro gradient at this face is g * volume[bottom], so
                    // gravity must match: g * volume[bottom] * dt.
                    gravityScale = std::clamp(waterVolume_[bottomIdx], 0.0f, 1.0f);
                }
                vFaceVelocity_[faceIdx] += gravityScale * gravity * dt;
            }
        }
    }

    captureAdvancePhaseSample(WaterAdvancePhase::AfterGravityPreStep, invDt);

    for (int y = 0; y < height_; ++y) {
        for (int x = 1; x < width_; ++x) {
            const size_t faceIdx = uFaceIndex(width_, x, y);
            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                continue;
            }
            if (uFaceWeight(x, y) <= kFaceWeightEpsilon) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            if (!waterAdvanceDebugOptions_.disableHydroPressureGradient) {
                bool skipHydro = false;
                if (waterAdvanceDebugOptions_.disableUFaceHydroNearInterface) {
                    constexpr float kPartialMin = 0.0001f;
                    constexpr float kPartialMax = 0.95f;
                    const float leftVol = std::clamp(waterVolume_[leftIdx], 0.0f, 1.0f);
                    const float rightVol = std::clamp(waterVolume_[rightIdx], 0.0f, 1.0f);
                    const bool leftPartial = leftVol > kPartialMin && leftVol < kPartialMax;
                    const bool rightPartial = rightVol > kPartialMin && rightVol < kPartialMax;
                    skipHydro = leftPartial || rightPartial;
                }
                if (!skipHydro) {
                    const float gradient =
                        (waterAdvanceDebugOptions_.useReconstructedHydroAssembly
                         && waterAdvanceDebugOptions_.useReconstructedFreeSurfaceGeometry)
                        ? uFaceHydroGradient_[faceIdx]
                        : (hydroPressure_[rightIdx] - hydroPressure_[leftIdx]);
                    uFaceVelocity_[faceIdx] -= dt * gradient;
                }
            }
        }
    }

    for (int y = 1; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t faceIdx = vFaceIndex(width_, x, y);
            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                continue;
            }
            if (vFaceWeight(x, y) <= kFaceWeightEpsilon) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            if (!waterAdvanceDebugOptions_.disableHydroPressureGradient) {
                float hydroScale = 1.0f;
                if (waterAdvanceDebugOptions_.scaleHydroGradientByVFaceFill) {
                    hydroScale = std::clamp(waterVolume_[bottomIdx], 0.0f, 1.0f);
                }
                vFaceVelocity_[faceIdx] -=
                    hydroScale * dt * (hydroPressure_[bottomIdx] - hydroPressure_[topIdx]);
            }
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x <= width_; ++x) {
            const size_t faceIdx = uFaceIndex(width_, x, y);
            if (x == 0 || x == width_) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }
            if (uFaceWeight(x, y) <= kFaceWeightEpsilon) {
                uFaceVelocity_[faceIdx] = 0.0f;
            }
        }
    }

    for (int y = 0; y <= height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t faceIdx = vFaceIndex(width_, x, y);
            if (y == 0 || y == height_) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }
            if (vFaceWeight(x, y) <= kFaceWeightEpsilon) {
                vFaceVelocity_[faceIdx] = 0.0f;
            }
        }
    }

    captureAdvancePhaseSample(WaterAdvancePhase::AfterHydroPressureGradient, invDt);

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            if (projectionMask_[idx] == 0) {
                divergence_[idx] = 0.0f;
                continue;
            }

            const float uRight = uFaceVelocity_[uFaceIndex(width_, x + 1, y)];
            const float uLeft = uFaceVelocity_[uFaceIndex(width_, x, y)];
            const float vDown = vFaceVelocity_[vFaceIndex(width_, x, y + 1)];
            const float vUp = vFaceVelocity_[vFaceIndex(width_, x, y)];
            const float uRightWeight = uFaceWeight(x + 1, y);
            const float uLeftWeight = uFaceWeight(x, y);
            const float vDownWeight = vFaceWeight(x, y + 1);
            const float vUpWeight = vFaceWeight(x, y);

            divergence_[idx] = ((uRightWeight * uRight) - (uLeftWeight * uLeft)
                                + (vDownWeight * vDown) - (vUpWeight * vUp))
                * invDt;
        }
    }

    std::fill(pressure_.begin(), pressure_.end(), 0.0f);
    std::fill(pressureScratch_.begin(), pressureScratch_.end(), 0.0f);

    const bool useGaussSeidel = waterAdvanceDebugOptions_.useGaussSeidelPressureSolver;
    const float sorOmega = waterAdvanceDebugOptions_.sorOmega;
    const int pressureIterations = waterAdvanceDebugOptions_.pressureIterationsOverride > 0
        ? waterAdvanceDebugOptions_.pressureIterationsOverride
        : parameters.pressureIterations;

    for (int iter = 0; iter < pressureIterations; ++iter) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t idx = cellIndex(width_, x, y);
                if (projectionMask_[idx] == 0) {
                    if (!useGaussSeidel) {
                        pressureScratch_[idx] = 0.0f;
                    }
                    continue;
                }

                float sum = 0.0f;
                float denom = 0.0f;

                const auto visitNeighbor =
                    [&](int nx, int ny, float faceWeight, float fluidAirBoundaryScale) {
                        if (faceWeight <= kFaceWeightEpsilon) {
                            return;
                        }
                        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) {
                            return;
                        }

                        const size_t nIdx = cellIndex(width_, nx, ny);
                        if (solidMask_[nIdx] != 0) {
                            return;
                        }

                        if (projectionMask_[nIdx] != 0) {
                            denom += faceWeight;
                            sum += faceWeight * pressure_[nIdx];
                        }
                        else if (!waterAdvanceDebugOptions_
                                      .excludeAirNeighborsFromPressureDenominator) {
                            denom += faceWeight * fluidAirBoundaryScale;
                        }
                    };

                visitNeighbor(x - 1, y, uFaceWeight(x, y), uFaceFluidAirBoundaryScale(x, y));
                visitNeighbor(
                    x + 1, y, uFaceWeight(x + 1, y), uFaceFluidAirBoundaryScale(x + 1, y));
                visitNeighbor(x, y - 1, vFaceWeight(x, y), vFaceFluidAirBoundaryScale(x, y));
                visitNeighbor(
                    x, y + 1, vFaceWeight(x, y + 1), vFaceFluidAirBoundaryScale(x, y + 1));

                if (denom <= 0.0f) {
                    if (useGaussSeidel) {
                        pressure_[idx] = 0.0f;
                    }
                    else {
                        pressureScratch_[idx] = 0.0f;
                    }
                    continue;
                }

                float divergenceTerm = divergence_[idx];
                if (waterAdvanceDebugOptions_.scaleProjectionDivergenceByCellFill) {
                    divergenceTerm *= std::clamp(waterVolume_[idx], 0.0f, 1.0f);
                }

                const float update = (sum - divergenceTerm) / denom;

                if (useGaussSeidel) {
                    pressure_[idx] = (sorOmega != 1.0f)
                        ? (1.0f - sorOmega) * pressure_[idx] + sorOmega * update
                        : update;
                }
                else {
                    pressureScratch_[idx] = update;
                }
            }
        }

        if (!useGaussSeidel) {
            std::swap(pressure_, pressureScratch_);
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x <= width_; ++x) {
            const size_t faceIdx = uFaceIndex(width_, x, y);
            if (x == 0 || x == width_) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }
            if (uFaceWeight(x, y) <= kFaceWeightEpsilon) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            if (projectionMask_[leftIdx] == 0 && projectionMask_[rightIdx] == 0) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const bool leftProjected = projectionMask_[leftIdx] != 0;
            const bool rightProjected = projectionMask_[rightIdx] != 0;
            const float pL = leftProjected ? pressure_[leftIdx] : 0.0f;
            const float pR = rightProjected ? pressure_[rightIdx] : 0.0f;
            float correctionScale = 1.0f;
            if (leftProjected != rightProjected) {
                correctionScale *= uFaceFluidAirBoundaryScale(x, y);
            }
            if (!useReconstructedFreeSurfaceGeometry
                && waterAdvanceDebugOptions_.scaleFluidAirPressureCorrectionByCellFill
                && (leftProjected != rightProjected)) {
                const size_t fluidIdx = leftProjected ? leftIdx : rightIdx;
                correctionScale = std::clamp(waterVolume_[fluidIdx], 0.0f, 1.0f);
                if (waterAdvanceDebugOptions_.treatFluidAirPressureBoundaryAtFace) {
                    correctionScale *= legacyFluidAirBoundaryScale;
                }
            }

            uFaceVelocity_[faceIdx] -=
                dt * parameters.pressureGradientVelocityScale * correctionScale * (pR - pL);
        }
    }

    for (int y = 0; y <= height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t faceIdx = vFaceIndex(width_, x, y);
            if (y == 0 || y == height_) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }
            if (vFaceWeight(x, y) <= kFaceWeightEpsilon) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            if (projectionMask_[topIdx] == 0 && projectionMask_[bottomIdx] == 0) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const bool topProjected = projectionMask_[topIdx] != 0;
            const bool bottomProjected = projectionMask_[bottomIdx] != 0;
            const float pT = topProjected ? pressure_[topIdx] : 0.0f;
            const float pB = bottomProjected ? pressure_[bottomIdx] : 0.0f;
            float correctionScale = 1.0f;
            if (topProjected != bottomProjected) {
                correctionScale *= vFaceFluidAirBoundaryScale(x, y);
            }
            if (!useReconstructedFreeSurfaceGeometry
                && waterAdvanceDebugOptions_.scaleFluidAirPressureCorrectionByCellFill
                && (topProjected != bottomProjected)) {
                const size_t fluidIdx = topProjected ? topIdx : bottomIdx;
                correctionScale = std::clamp(waterVolume_[fluidIdx], 0.0f, 1.0f);
                if (waterAdvanceDebugOptions_.treatFluidAirPressureBoundaryAtFace) {
                    correctionScale *= legacyFluidAirBoundaryScale;
                }
            }

            vFaceVelocity_[faceIdx] -=
                dt * parameters.pressureGradientVelocityScale * correctionScale * (pB - pT);
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x <= width_; ++x) {
            const size_t faceIdx = uFaceIndex(width_, x, y);
            if (x == 0 || x == width_) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }
            if (uFaceWeight(x, y) <= kFaceWeightEpsilon) {
                uFaceVelocity_[faceIdx] = 0.0f;
            }
        }
    }

    for (int y = 0; y <= height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t faceIdx = vFaceIndex(width_, x, y);
            if (y == 0 || y == height_) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }
            if (vFaceWeight(x, y) <= kFaceWeightEpsilon) {
                vFaceVelocity_[faceIdx] = 0.0f;
            }
        }
    }

    captureAdvancePhaseSample(WaterAdvancePhase::AfterPressureProjection, invDt);

    for (const GuidedWaterDrain& drain : pendingGuidedWaterDrains_) {
        applyGuidedWaterDrainVelocityBias(drain);
    }

    const float dampingFactor =
        std::clamp(1.0f - parameters.velocityDampingPerSecond * dt, 0.0f, 1.0f);
    const float maxFaceSpeed = parameters.velocityCflLimit / dt;

    for (float& u : uFaceVelocity_) {
        if (!std::isfinite(u)) {
            u = 0.0f;
            continue;
        }

        u *= dampingFactor;
        u = std::clamp(u, -maxFaceSpeed, maxFaceSpeed);
        if (std::abs(u) < parameters.velocitySleepEpsilon) {
            u = 0.0f;
        }
    }

    for (float& v : vFaceVelocity_) {
        if (!std::isfinite(v)) {
            v = 0.0f;
            continue;
        }

        v *= dampingFactor;
        v = std::clamp(v, -maxFaceSpeed, maxFaceSpeed);
        if (std::abs(v) < parameters.velocitySleepEpsilon) {
            v = 0.0f;
        }
    }

    captureAdvancePhaseSample(WaterAdvancePhase::AfterDamping, invDt);

    // Advect from the pre-step volume field into a separate output field.
    // The prior in-place update plus 8x horizontal scaling was turning coherent
    // pools into a low-density haze across the basin.
    volumeScratch_ = waterVolume_;

    const float advectionVolumeEpsilon =
        pendingGuidedWaterDrains_.empty() ? parameters.advectionVolumeEpsilon : 0.0f;

    for (int y = 0; y < height_; ++y) {
        for (int x = 1; x < width_; ++x) {
            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                continue;
            }

            const size_t faceIdx = uFaceIndex(width_, x, y);
            const float u = uFaceVelocity_[faceIdx];
            if (u == 0.0f) {
                continue;
            }
            const float faceWeight = uFaceWeight(x, y);
            if (faceWeight <= kFaceWeightEpsilon) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const float cfl =
                std::clamp(u * dt, -parameters.advectionCflLimit, parameters.advectionCflLimit);
            if (cfl == 0.0f) {
                continue;
            }

            const bool flowRight = u > 0.0f;
            const size_t donorIdx = flowRight ? leftIdx : rightIdx;
            const size_t receiverIdx = flowRight ? rightIdx : leftIdx;

            const float donorSourceVol = waterVolume_[donorIdx];
            if (donorSourceVol <= advectionVolumeEpsilon) {
                continue;
            }

            const float donorAvailable = volumeScratch_[donorIdx];
            if (donorAvailable <= advectionVolumeEpsilon) {
                continue;
            }

            const float receiverCapacity = std::max(0.0f, 1.0f - volumeScratch_[receiverIdx]);
            if (receiverCapacity <= 0.0f) {
                continue;
            }

            float desired = std::abs(cfl) * donorSourceVol;
            if (useReconstructedFreeSurfaceGeometry) {
                desired *= faceWeight;
            }
            const float transfer = std::min(std::min(desired, donorAvailable), receiverCapacity);
            if (transfer <= 0.0f) {
                continue;
            }

            volumeScratch_[donorIdx] -= transfer;
            volumeScratch_[receiverIdx] += transfer;
        }
    }

    for (int y = 1; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t topIdx = cellIndex(width_, x, y - 1);
            const size_t bottomIdx = cellIndex(width_, x, y);
            if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                continue;
            }

            const size_t faceIdx = vFaceIndex(width_, x, y);
            const float v = vFaceVelocity_[faceIdx];
            if (v == 0.0f) {
                continue;
            }
            const float faceWeight = vFaceWeight(x, y);
            if (faceWeight <= kFaceWeightEpsilon) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const float cfl =
                std::clamp(v * dt, -parameters.advectionCflLimit, parameters.advectionCflLimit);
            if (cfl == 0.0f) {
                continue;
            }

            const bool flowDown = v > 0.0f;
            const size_t donorIdx = flowDown ? topIdx : bottomIdx;
            const size_t receiverIdx = flowDown ? bottomIdx : topIdx;

            const float donorSourceVol = waterVolume_[donorIdx];
            if (donorSourceVol <= advectionVolumeEpsilon) {
                continue;
            }

            const float donorAvailable = volumeScratch_[donorIdx];
            if (donorAvailable <= advectionVolumeEpsilon) {
                continue;
            }

            const float receiverCapacity = std::max(0.0f, 1.0f - volumeScratch_[receiverIdx]);
            if (receiverCapacity <= 0.0f) {
                continue;
            }

            float desired = std::abs(cfl) * donorSourceVol;
            if (useReconstructedFreeSurfaceGeometry) {
                desired *= faceWeight;
            }
            const float transfer = std::min(std::min(desired, donorAvailable), receiverCapacity);
            if (transfer <= 0.0f) {
                continue;
            }

            volumeScratch_[donorIdx] -= transfer;
            volumeScratch_[receiverIdx] += transfer;
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] != 0) {
                waterVolume_[idx] = 0.0f;
                continue;
            }

            waterVolume_[idx] = std::clamp(volumeScratch_[idx], 0.0f, 1.0f);
        }
    }

    captureAdvancePhaseSample(WaterAdvancePhase::AfterAdvection, invDt);

    settleResidualWater();
    captureAdvancePhaseSample(WaterAdvancePhase::AfterResidualSettle, invDt);

    for (const GuidedWaterDrain& drain : pendingGuidedWaterDrains_) {
        applyGuidedWaterDrainOutflow(drain, dt);
    }
    pendingGuidedWaterDrains_.clear();
    captureAdvancePhaseSample(WaterAdvancePhase::Final, invDt);

    // Frozen-state diagnostic: restore volumes to prevent transport/topology changes.
    if (freezeVolume) {
        waterVolume_ = frozenVolume;
    }

    // Flush subnormal residue to zero. These values are effectively numerical noise but can
    // persist forever (and keep drain/presence logic latched).
    constexpr float kMinNormal = std::numeric_limits<float>::min();
    for (float& volume : waterVolume_) {
        if (!std::isfinite(volume) || (volume > 0.0f && volume < kMinNormal)) {
            volume = 0.0f;
        }
    }

    rebuildWaterActivityView(previousWaterVolume_);
}

void MacProjectionWaterSim::rebuildWaterSleepShadowStats(const World& world)
{
    waterSleepShadowStats_ = {};
    if (width_ <= 0 || height_ <= 0) {
        return;
    }

    constexpr float kVolumeEpsilon = 0.0001f;
    constexpr int kRegionSize = 8;
    const int blocksX = std::max(0, (width_ + 7) / 8);
    const int blocksY = std::max(0, (height_ + 7) / 8);
    waterSleepShadowStats_.blocksX = blocksX;
    waterSleepShadowStats_.blocksY = blocksY;

    const size_t regionCount = static_cast<size_t>(blocksX) * static_cast<size_t>(blocksY);
    std::vector<uint8_t> regionHasWater(regionCount, 0);

    const WorldRegionActivityTracker& tracker = world.getRegionActivityTracker();

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] != 0 || waterVolume_[idx] <= kVolumeEpsilon) {
                continue;
            }

            waterSleepShadowStats_.totalWaterCells++;

            const int blockX = x / kRegionSize;
            const int blockY = y / kRegionSize;
            const int regionIdx = blockY * blocksX + blockX;
            if (regionIdx >= 0 && static_cast<size_t>(regionIdx) < regionHasWater.size()) {
                regionHasWater[static_cast<size_t>(regionIdx)] = 1;
            }

            if (!tracker.wouldRegionBeActiveThisFrame(blockX, blockY)) {
                waterSleepShadowStats_.shadowSkippableWaterCells++;
            }
        }
    }

    for (int blockY = 0; blockY < blocksY; ++blockY) {
        for (int blockX = 0; blockX < blocksX; ++blockX) {
            const int regionIdx = blockY * blocksX + blockX;
            if (regionIdx < 0 || static_cast<size_t>(regionIdx) >= regionHasWater.size()
                || regionHasWater[static_cast<size_t>(regionIdx)] == 0) {
                continue;
            }

            waterSleepShadowStats_.totalWaterRegions++;
            if (tracker.wouldRegionBeActiveThisFrame(blockX, blockY)) {
                waterSleepShadowStats_.shadowActiveWaterRegions++;
            }
            else {
                waterSleepShadowStats_.shadowSkippableWaterRegions++;
            }
        }
    }
}

void MacProjectionWaterSim::rebuildWaterActivityView(const std::vector<float>& previousWaterVolume)
{
    const auto cellHasFluid = [&](int x, int y) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return false;
        }

        const size_t idx = cellIndex(width_, x, y);
        return solidMask_[idx] == 0 && waterVolume_[idx] > parameters_.fluidMaskVolumeEpsilon;
    };

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            if (solidMask_[idx] != 0) {
                waterActivityMaxFaceSpeed_[idx] = 0.0f;
                waterActivityVolumeDelta_[idx] = 0.0f;
                waterActivityFlags_[idx] = 0;
                continue;
            }

            const float currentVolume = std::clamp(waterVolume_[idx], 0.0f, 1.0f);
            const float previousVolume = idx < previousWaterVolume.size()
                ? std::clamp(previousWaterVolume[idx], 0.0f, 1.0f)
                : 0.0f;

            waterActivityVolumeDelta_[idx] = std::abs(currentVolume - previousVolume);
            waterActivityMaxFaceSpeed_[idx] = std::max(
                {
                    std::abs(uFaceVelocity_[uFaceIndex(width_, x, y)]),
                    std::abs(uFaceVelocity_[uFaceIndex(width_, x + 1, y)]),
                    std::abs(vFaceVelocity_[vFaceIndex(width_, x, y)]),
                    std::abs(vFaceVelocity_[vFaceIndex(width_, x, y + 1)]),
                });

            uint8_t flags = 0;
            const bool hasFluid = cellHasFluid(x, y);
            if (hasFluid) {
                flags |= static_cast<uint8_t>(WaterActivityFlag::HasFluid);
            }

            if (hasFluid) {
                const auto neighborIsInterface = [&](int nx, int ny) {
                    if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) {
                        return true;
                    }

                    return !cellHasFluid(nx, ny);
                };

                if (neighborIsInterface(x - 1, y) || neighborIsInterface(x + 1, y)
                    || neighborIsInterface(x, y - 1) || neighborIsInterface(x, y + 1)) {
                    flags |= static_cast<uint8_t>(WaterActivityFlag::Interface);
                }
            }

            waterActivityFlags_[idx] = flags;
        }
    }
}

void MacProjectionWaterSim::settleResidualWater()
{
    const float settleEpsilon = std::max(0.0f, parameters_.fluidMaskVolumeEpsilon);
    if (settleEpsilon <= 0.0f) {
        return;
    }

    // Water below the MAC fluid threshold is not part of the pressure solve and can linger as a
    // low-density haze. Treat it like droplets and let it settle under gravity so it can either
    // pool or drain.
    for (int y = height_ - 2; y >= 0; --y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            const size_t belowIdx = cellIndex(width_, x, y + 1);
            if (solidMask_[idx] != 0 || solidMask_[belowIdx] != 0) {
                continue;
            }

            const float volume = waterVolume_[idx];
            if (volume <= 0.0f || volume >= settleEpsilon) {
                continue;
            }

            const float belowVolume = waterVolume_[belowIdx];
            const float capacity = std::max(0.0f, 1.0f - belowVolume);
            if (capacity <= 0.0f) {
                continue;
            }

            const float transfer = std::min(volume, capacity);
            waterVolume_[idx] -= transfer;
            waterVolume_[belowIdx] += transfer;
        }
    }
}

void MacProjectionWaterSim::applyGuidedWaterDrainOutflow(const GuidedWaterDrain& drain, float dt)
{
    if (drain.drainRatePerSecond <= 0.0f || drain.mouthY < 0 || drain.mouthY >= height_) {
        return;
    }

    const int mouthStartX = std::max(0, static_cast<int>(drain.mouthStartX));
    const int mouthEndX = std::min(width_ - 1, static_cast<int>(drain.mouthEndX));
    if (mouthStartX > mouthEndX) {
        return;
    }

    const float drainAmount = drain.drainRatePerSecond * dt;
    if (drainAmount <= 0.0f) {
        return;
    }

    for (int x = mouthStartX; x <= mouthEndX; ++x) {
        const size_t idx = cellIndex(width_, x, drain.mouthY);
        if (solidMask_[idx] != 0) {
            continue;
        }

        waterVolume_[idx] = std::max(0.0f, waterVolume_[idx] - drainAmount);
    }
}

void MacProjectionWaterSim::applyGuidedWaterDrainVelocityBias(const GuidedWaterDrain& drain)
{
    if (drain.guideDownwardSpeed <= 0.0f && drain.guideLateralSpeed <= 0.0f
        && drain.mouthDownwardSpeed <= 0.0f) {
        return;
    }

    const int guideStartX = std::max(0, static_cast<int>(drain.guideStartX));
    const int guideEndX = std::min(width_ - 1, static_cast<int>(drain.guideEndX));
    const int guideTopY = std::max(0, static_cast<int>(drain.guideTopY));
    const int guideBottomY = std::min(height_ - 1, static_cast<int>(drain.guideBottomY));
    const int mouthStartX = std::max(0, static_cast<int>(drain.mouthStartX));
    const int mouthEndX = std::min(width_ - 1, static_cast<int>(drain.mouthEndX));
    if (guideStartX > guideEndX || guideTopY > guideBottomY || mouthStartX > mouthEndX) {
        return;
    }

    if (drain.guideDownwardSpeed > 0.0f) {
        const int rowsAboveBottom = guideBottomY - guideTopY;
        for (int y = guideTopY; y < guideBottomY; ++y) {
            float rowStrength = drain.guideDownwardSpeed;
            if (rowsAboveBottom > 0) {
                const float normalized =
                    static_cast<float>(y - guideTopY + 1) / static_cast<float>(rowsAboveBottom);
                rowStrength *= normalized;
            }

            for (int x = guideStartX; x <= guideEndX; ++x) {
                const size_t topIdx = cellIndex(width_, x, y);
                const size_t bottomIdx = cellIndex(width_, x, y + 1);
                if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
                    continue;
                }

                const size_t faceIdx = vFaceIndex(width_, x, y + 1);
                vFaceVelocity_[faceIdx] = std::max(vFaceVelocity_[faceIdx], rowStrength);
            }
        }
    }

    if (drain.guideLateralSpeed > 0.0f) {
        const int guideY = guideBottomY;
        for (int faceX = guideStartX + 1; faceX <= guideEndX; ++faceX) {
            const int leftX = faceX - 1;
            const int rightX = faceX;
            const size_t leftIdx = cellIndex(width_, leftX, guideY);
            const size_t rightIdx = cellIndex(width_, rightX, guideY);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                continue;
            }

            const size_t faceIdx = uFaceIndex(width_, faceX, guideY);
            if (faceX <= mouthStartX) {
                uFaceVelocity_[faceIdx] =
                    std::max(uFaceVelocity_[faceIdx], drain.guideLateralSpeed);
            }
            else if (faceX > mouthEndX) {
                uFaceVelocity_[faceIdx] =
                    std::min(uFaceVelocity_[faceIdx], -drain.guideLateralSpeed);
            }
        }
    }

    if (drain.mouthDownwardSpeed <= 0.0f || drain.mouthY <= 0 || drain.mouthY > height_ - 1) {
        return;
    }

    for (int x = mouthStartX; x <= mouthEndX; ++x) {
        const size_t topIdx = cellIndex(width_, x, drain.mouthY - 1);
        const size_t bottomIdx = cellIndex(width_, x, drain.mouthY);
        if (solidMask_[topIdx] != 0 || solidMask_[bottomIdx] != 0) {
            continue;
        }

        const size_t faceIdx = vFaceIndex(width_, x, drain.mouthY);
        vFaceVelocity_[faceIdx] = std::max(vFaceVelocity_[faceIdx], drain.mouthDownwardSpeed);
    }
}

} // namespace DirtSim
