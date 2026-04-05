#include "MazeModel.h"
#include "core/Assert.h"
#include <array>

namespace DirtSim {
namespace Ui {
namespace {

constexpr std::array<MazeCoord, 4> kDirectionDeltas = {
    MazeCoord{ 0, -1 },
    MazeCoord{ 1, 0 },
    MazeCoord{ 0, 1 },
    MazeCoord{ -1, 0 },
};

constexpr std::array<MazeDirection, 4> kOppositeDirections = {
    MazeDirection::South,
    MazeDirection::West,
    MazeDirection::North,
    MazeDirection::East,
};

} // namespace

uint8_t mazeDirectionBit(MazeDirection direction)
{
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(direction));
}

MazeCoord mazeDirectionDelta(MazeDirection direction)
{
    return kDirectionDeltas.at(static_cast<size_t>(direction));
}

MazeDirection mazeOppositeDirection(MazeDirection direction)
{
    return kOppositeDirections.at(static_cast<size_t>(direction));
}

MazeModel::MazeModel(int width, int height)
    : cells_(static_cast<size_t>(width * height)),
      goalIndex_(width * height - 1),
      height_(height),
      width_(width)
{
    DIRTSIM_ASSERT(width > 0, "MazeModel width must be positive");
    DIRTSIM_ASSERT(height > 0, "MazeModel height must be positive");
}

const MazeCell& MazeModel::cellAt(int index) const
{
    DIRTSIM_ASSERT(index >= 0, "MazeModel index must be non-negative");
    DIRTSIM_ASSERT(index < cellCount(), "MazeModel index out of range");
    return cells_.at(static_cast<size_t>(index));
}

MazeCell& MazeModel::cellAt(int index)
{
    DIRTSIM_ASSERT(index >= 0, "MazeModel index must be non-negative");
    DIRTSIM_ASSERT(index < cellCount(), "MazeModel index out of range");
    return cells_.at(static_cast<size_t>(index));
}

MazeCoord MazeModel::coordForIndex(int index) const
{
    DIRTSIM_ASSERT(index >= 0, "MazeModel index must be non-negative");
    DIRTSIM_ASSERT(index < cellCount(), "MazeModel index out of range");
    return MazeCoord{
        .x = index % width_,
        .y = index / width_,
    };
}

int MazeModel::indexForCoord(MazeCoord coord) const
{
    DIRTSIM_ASSERT(isValidCoord(coord), "MazeModel coord out of range");
    return coord.y * width_ + coord.x;
}

bool MazeModel::isValidCoord(MazeCoord coord) const
{
    return coord.x >= 0 && coord.y >= 0 && coord.x < width_ && coord.y < height_;
}

void MazeModel::setEndpoints(int startIndex, int goalIndex)
{
    DIRTSIM_ASSERT(startIndex >= 0, "MazeModel start index must be non-negative");
    DIRTSIM_ASSERT(startIndex < cellCount(), "MazeModel start index out of range");
    DIRTSIM_ASSERT(goalIndex >= 0, "MazeModel goal index must be non-negative");
    DIRTSIM_ASSERT(goalIndex < cellCount(), "MazeModel goal index out of range");

    startIndex_ = startIndex;
    goalIndex_ = goalIndex;
}

} // namespace Ui
} // namespace DirtSim
