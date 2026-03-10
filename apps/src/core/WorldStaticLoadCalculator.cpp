#include "WorldStaticLoadCalculator.h"

#include "Cell.h"
#include "PhysicsSettings.h"
#include "World.h"
#include "WorldData.h"

#include <array>
#include <cmath>
#include <vector>

namespace {

using DirtSim::Cell;
using DirtSim::Material::EnumType;

bool isLoadBearingGranular(const Cell& cell)
{
    if (cell.isEmpty()) {
        return false;
    }

    switch (cell.material_type) {
        case EnumType::Air:
        case EnumType::Leaf:
        case EnumType::Metal:
        case EnumType::Root:
        case EnumType::Seed:
        case EnumType::Wall:
        case EnumType::Water:
        case EnumType::Wood:
            return false;
        case EnumType::Dirt:
        case EnumType::Sand:
            return true;
    }

    return false;
}

bool isSupportSink(const Cell& cell)
{
    if (cell.isEmpty()) {
        return false;
    }

    switch (cell.material_type) {
        case EnumType::Air:
        case EnumType::Dirt:
        case EnumType::Leaf:
        case EnumType::Root:
        case EnumType::Sand:
        case EnumType::Seed:
        case EnumType::Water:
            return false;
        case EnumType::Metal:
        case EnumType::Wall:
        case EnumType::Wood:
            return true;
    }

    return false;
}

} // namespace

namespace DirtSim {

void WorldStaticLoadCalculator::recomputeAll(World& world) const
{
    WorldData& data = world.getData();

    for (Cell& cell : data.cells) {
        cell.static_load = 0.0f;
    }

    const double gravity = world.getPhysicsSettings().gravity;
    if (std::abs(gravity) < MIN_GRAVITY_THRESHOLD) {
        return;
    }

    std::vector<float> incomingLoad(data.cells.size(), 0.0f);

    const float gravityMagnitude = static_cast<float>(std::abs(gravity));
    const int supportOffsetY = gravity > 0.0 ? 1 : -1;
    const int yBegin = gravity > 0.0 ? 0 : data.height - 1;
    const int yEnd = gravity > 0.0 ? data.height : -1;
    const int yStep = gravity > 0.0 ? 1 : -1;

    for (int y = yBegin; y != yEnd; y += yStep) {
        for (int x = 0; x < data.width; ++x) {
            Cell& cell = data.at(x, y);
            if (!isLoadBearingGranular(cell)) {
                continue;
            }

            const size_t cellIndex = static_cast<size_t>(y) * data.width + x;
            const float selfWeight = cell.getMass() * gravityMagnitude;
            const float totalLoad = selfWeight + incomingLoad[cellIndex];
            cell.static_load = totalLoad;

            const int supportY = y + supportOffsetY;
            if (!data.inBounds(x, supportY)) {
                continue;
            }

            const Cell& directSupport = data.at(x, supportY);
            if (isLoadBearingGranular(directSupport)) {
                const size_t directIndex = static_cast<size_t>(supportY) * data.width + x;
                incomingLoad[directIndex] += totalLoad;
                continue;
            }

            if (isSupportSink(directSupport)) {
                continue;
            }

            std::array<int, 2> diagonalXs = { x - 1, x + 1 };
            std::array<size_t, 2> diagonalIndices = {};
            size_t diagonalCount = 0;

            for (const int diagonalX : diagonalXs) {
                if (!data.inBounds(diagonalX, supportY)) {
                    continue;
                }

                const Cell& diagonalSupport = data.at(diagonalX, supportY);
                if (!isLoadBearingGranular(diagonalSupport)) {
                    continue;
                }

                diagonalIndices[diagonalCount++] =
                    static_cast<size_t>(supportY) * data.width + diagonalX;
            }

            if (diagonalCount == 1) {
                incomingLoad[diagonalIndices[0]] += totalLoad;
                continue;
            }

            if (diagonalCount == 2) {
                const float splitLoad = totalLoad * 0.5f;
                incomingLoad[diagonalIndices[0]] += splitLoad;
                incomingLoad[diagonalIndices[1]] += splitLoad;
            }
        }
    }
}

} // namespace DirtSim
