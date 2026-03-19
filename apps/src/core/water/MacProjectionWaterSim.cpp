#include "MacProjectionWaterSim.h"

#include <algorithm>
#include <cmath>

#include "core/Cell.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"

namespace DirtSim {
namespace {

constexpr float kAdvectionVolumeEpsilon = 0.0001f;
constexpr float kFluidMaskVolumeEpsilon = 0.0001f;
constexpr int kPressureIterations = 60;
constexpr float kPressureGradientVelocityScale = 1.0f;
constexpr float kVelocityDampingPerSecond = 0.20f;
constexpr float kVelocitySleepEpsilon = 0.00005f;
constexpr float kVelocityCflLimit = 0.95f;
constexpr float kHorizontalAdvectionScale = 8.0f;
constexpr float kVerticalAdvectionScale = 1.0f;
constexpr int kDisplacementMaxRadius = 8;

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

            for (int radius = 2; radius <= kDisplacementMaxRadius && remaining > 0.0f; ++radius) {
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
                (solidMask_[idx] == 0 && waterVolume_[idx] > kFluidMaskVolumeEpsilon) ? 1 : 0;
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

    constexpr int kProjectionShellRadius = 2;
    for (int iter = 0; iter < kProjectionShellRadius; ++iter) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const size_t idx = cellIndex(width_, x, y);
                if (solidMask_[idx] != 0) {
                    projectionMaskScratch_[idx] = 0;
                    continue;
                }

                if (projectionMask_[idx] != 0) {
                    projectionMaskScratch_[idx] = 1;
                    continue;
                }

                bool neighborActive = false;
                if (x > 0 && projectionMask_[idx - 1] != 0) {
                    neighborActive = true;
                }
                else if (x + 1 < width_ && projectionMask_[idx + 1] != 0) {
                    neighborActive = true;
                }
                else if (y > 0 && projectionMask_[idx - static_cast<size_t>(width_)] != 0) {
                    neighborActive = true;
                }
                else if (
                    y + 1 < height_ && projectionMask_[idx + static_cast<size_t>(width_)] != 0) {
                    neighborActive = true;
                }

                projectionMaskScratch_[idx] = neighborActive ? 1 : 0;
            }
        }

        std::swap(projectionMask_, projectionMaskScratch_);
    }

    if (totalWaterVolume <= 0.0f) {
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

    for (int iter = 0; iter < kPressureIterations; ++iter) {
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

                    if (projectionMask_[nIdx] == 0) {
                        return;
                    }

                    sum += pressure_[nIdx];
                    denom += 1.0f;
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
            uFaceVelocity_[faceIdx] -= dt * kPressureGradientVelocityScale * (pR - pL);
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
            vFaceVelocity_[faceIdx] -= dt * kPressureGradientVelocityScale * (pB - pT);
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

    const float dampingFactor = std::clamp(1.0f - kVelocityDampingPerSecond * dt, 0.0f, 1.0f);
    const float maxFaceSpeed = kVelocityCflLimit / dt;

    for (float& u : uFaceVelocity_) {
        if (!std::isfinite(u)) {
            u = 0.0f;
            continue;
        }

        u *= dampingFactor;
        u = std::clamp(u, -maxFaceSpeed, maxFaceSpeed);
        if (std::abs(u) < kVelocitySleepEpsilon) {
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
        if (std::abs(v) < kVelocitySleepEpsilon) {
            v = 0.0f;
        }
    }

    volumeScratch_ = waterVolume_;

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

            const bool flowRight = u > 0.0f;
            const size_t donorIdx = flowRight ? leftIdx : rightIdx;
            const size_t receiverIdx = flowRight ? rightIdx : leftIdx;

            const float donorVol = volumeScratch_[donorIdx];
            if (donorVol <= kAdvectionVolumeEpsilon) {
                continue;
            }

            const float receiverVol = volumeScratch_[receiverIdx];
            const float receiverCapacity = std::max(0.0f, 1.0f - receiverVol);
            if (receiverCapacity <= 0.0f) {
                continue;
            }

            const float desired = std::abs(u) * dt * kHorizontalAdvectionScale * donorVol;
            const float transfer = std::min(std::min(desired, donorVol), receiverCapacity);
            if (transfer <= 0.0f) {
                continue;
            }

            volumeScratch_[donorIdx] = donorVol - transfer;
            volumeScratch_[receiverIdx] = receiverVol + transfer;
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

            const bool flowDown = v > 0.0f;
            const size_t donorIdx = flowDown ? topIdx : bottomIdx;
            const size_t receiverIdx = flowDown ? bottomIdx : topIdx;

            const float donorVol = volumeScratch_[donorIdx];
            if (donorVol <= kAdvectionVolumeEpsilon) {
                continue;
            }

            const float receiverVol = volumeScratch_[receiverIdx];
            const float receiverCapacity = std::max(0.0f, 1.0f - receiverVol);
            if (receiverCapacity <= 0.0f) {
                continue;
            }

            const float desired = std::abs(v) * dt * kVerticalAdvectionScale * donorVol;
            const float transfer = std::min(std::min(desired, donorVol), receiverCapacity);
            if (transfer <= 0.0f) {
                continue;
            }

            volumeScratch_[donorIdx] = donorVol - transfer;
            volumeScratch_[receiverIdx] = receiverVol + transfer;
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
}

} // namespace DirtSim
