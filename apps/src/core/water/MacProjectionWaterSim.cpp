#include "MacProjectionWaterSim.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/Cell.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"

namespace DirtSim {
namespace {

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

} // namespace

void MacProjectionWaterSim::reset()
{
    pendingGuidedWaterDrains_.clear();
    std::fill(waterVolume_.begin(), waterVolume_.end(), 0.0f);
    std::fill(uFaceVelocity_.begin(), uFaceVelocity_.end(), 0.0f);
    std::fill(vFaceVelocity_.begin(), vFaceVelocity_.end(), 0.0f);
    std::fill(divergence_.begin(), divergence_.end(), 0.0f);
    std::fill(pressure_.begin(), pressure_.end(), 0.0f);
    std::fill(pressureScratch_.begin(), pressureScratch_.end(), 0.0f);
    std::fill(hydroPressure_.begin(), hydroPressure_.end(), 0.0f);
    std::fill(volumeScratch_.begin(), volumeScratch_.end(), 0.0f);
    std::fill(fluidMask_.begin(), fluidMask_.end(), 0);
    std::fill(projectionMask_.begin(), projectionMask_.end(), 0);
    std::fill(projectionMaskScratch_.begin(), projectionMaskScratch_.end(), 0);
    std::fill(solidMask_.begin(), solidMask_.end(), 0);
}

void MacProjectionWaterSim::resize(int worldWidth, int worldHeight)
{
    width_ = worldWidth;
    height_ = worldHeight;
    pendingGuidedWaterDrains_.clear();

    const size_t cellCount = static_cast<size_t>(width_) * height_;
    const size_t uFaceCount = static_cast<size_t>(width_ + 1) * height_;
    const size_t vFaceCount = static_cast<size_t>(width_) * (height_ + 1);

    waterVolume_.assign(cellCount, 0.0f);
    uFaceVelocity_.assign(uFaceCount, 0.0f);
    vFaceVelocity_.assign(vFaceCount, 0.0f);
    divergence_.assign(cellCount, 0.0f);
    pressure_.assign(cellCount, 0.0f);
    pressureScratch_.assign(cellCount, 0.0f);
    hydroPressure_.assign(cellCount, 0.0f);
    volumeScratch_.assign(cellCount, 0.0f);
    fluidMask_.assign(cellCount, 0);
    projectionMask_.assign(cellCount, 0);
    projectionMaskScratch_.assign(cellCount, 0);
    solidMask_.assign(cellCount, 0);
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

void MacProjectionWaterSim::advanceTime(World& world, double deltaTimeSeconds)
{
    if (width_ <= 0 || height_ <= 0) {
        return;
    }
    if (deltaTimeSeconds <= 0.0) {
        return;
    }

    const float dt = static_cast<float>(deltaTimeSeconds);
    const WorldData& data = world.getData();

    const float gravity = static_cast<float>(world.getPhysicsSettings().gravity);
    const Parameters& parameters = parameters_;

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = cellIndex(width_, x, y);
            const Cell& cell = data.at(x, y);
            const bool solid = !cell.isEmpty() && cell.material_type != Material::EnumType::Air;
            solidMask_[idx] = solid ? 1 : 0;
        }
    }

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

    if (totalWaterVolume <= 0.0f) {
        pendingGuidedWaterDrains_.clear();
        std::fill(uFaceVelocity_.begin(), uFaceVelocity_.end(), 0.0f);
        std::fill(vFaceVelocity_.begin(), vFaceVelocity_.end(), 0.0f);
        return;
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

            if (fluidMask_[topIdx] == 0 && fluidMask_[bottomIdx] == 0) {
                continue;
            }

            vFaceVelocity_[faceIdx] += gravity * dt;
        }
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 1; x < width_; ++x) {
            const size_t faceIdx = uFaceIndex(width_, x, y);
            const size_t leftIdx = cellIndex(width_, x - 1, y);
            const size_t rightIdx = cellIndex(width_, x, y);
            if (solidMask_[leftIdx] != 0 || solidMask_[rightIdx] != 0) {
                continue;
            }

            uFaceVelocity_[faceIdx] -= dt * (hydroPressure_[rightIdx] - hydroPressure_[leftIdx]);
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

            vFaceVelocity_[faceIdx] -= dt * (hydroPressure_[bottomIdx] - hydroPressure_[topIdx]);
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
            }
        }
    }

    const float invDt = 1.0f / dt;
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

            divergence_[idx] = (uRight - uLeft + vDown - vUp) * invDt;
        }
    }

    std::fill(pressure_.begin(), pressure_.end(), 0.0f);
    std::fill(pressureScratch_.begin(), pressureScratch_.end(), 0.0f);

    for (int iter = 0; iter < parameters.pressureIterations; ++iter) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t idx = cellIndex(width_, x, y);
                if (projectionMask_[idx] == 0) {
                    pressureScratch_[idx] = 0.0f;
                    continue;
                }

                float sum = 0.0f;
                float denom = 0.0f;

                const auto visitNeighbor = [&](int nx, int ny) {
                    if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) {
                        return;
                    }

                    const size_t nIdx = cellIndex(width_, nx, ny);
                    if (solidMask_[nIdx] != 0) {
                        return;
                    }

                    denom += 1.0f;
                    if (projectionMask_[nIdx] != 0) {
                        sum += pressure_[nIdx];
                    }
                };

                visitNeighbor(x - 1, y);
                visitNeighbor(x + 1, y);
                visitNeighbor(x, y - 1);
                visitNeighbor(x, y + 1);

                if (denom <= 0.0f) {
                    pressureScratch_[idx] = 0.0f;
                    continue;
                }

                pressureScratch_[idx] = (sum - divergence_[idx]) / denom;
            }
        }

        std::swap(pressure_, pressureScratch_);
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

            if (projectionMask_[leftIdx] == 0 && projectionMask_[rightIdx] == 0) {
                uFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const float pL = projectionMask_[leftIdx] != 0 ? pressure_[leftIdx] : 0.0f;
            const float pR = projectionMask_[rightIdx] != 0 ? pressure_[rightIdx] : 0.0f;
            uFaceVelocity_[faceIdx] -= dt * parameters.pressureGradientVelocityScale * (pR - pL);
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

            if (projectionMask_[topIdx] == 0 && projectionMask_[bottomIdx] == 0) {
                vFaceVelocity_[faceIdx] = 0.0f;
                continue;
            }

            const float pT = projectionMask_[topIdx] != 0 ? pressure_[topIdx] : 0.0f;
            const float pB = projectionMask_[bottomIdx] != 0 ? pressure_[bottomIdx] : 0.0f;
            vFaceVelocity_[faceIdx] -= dt * parameters.pressureGradientVelocityScale * (pB - pT);
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
            }
        }
    }

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

            const float desired = std::abs(cfl) * donorSourceVol;
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

            const float desired = std::abs(cfl) * donorSourceVol;
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

    settleResidualWater();

    for (const GuidedWaterDrain& drain : pendingGuidedWaterDrains_) {
        applyGuidedWaterDrainOutflow(drain, dt);
    }
    pendingGuidedWaterDrains_.clear();

    // Flush subnormal residue to zero. These values are effectively numerical noise but can
    // persist forever (and keep drain/presence logic latched).
    constexpr float kMinNormal = std::numeric_limits<float>::min();
    for (float& volume : waterVolume_) {
        if (!std::isfinite(volume) || (volume > 0.0f && volume < kMinNormal)) {
            volume = 0.0f;
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
