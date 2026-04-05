#pragma once

#include <cstdint>
#include <vector>

namespace DirtSim {
namespace Ui {

enum class MazeDirection : uint8_t {
    North = 0,
    East = 1,
    South = 2,
    West = 3,
};

struct MazeCoord {
    int x = 0;
    int y = 0;
};

struct MazeCell {
    uint8_t openings = 0;
};

uint8_t mazeDirectionBit(MazeDirection direction);
MazeCoord mazeDirectionDelta(MazeDirection direction);
MazeDirection mazeOppositeDirection(MazeDirection direction);

class MazeModel {
public:
    MazeModel() = default;
    MazeModel(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    int cellCount() const { return static_cast<int>(cells_.size()); }

    const MazeCell& cellAt(int index) const;
    MazeCell& cellAt(int index);
    MazeCoord coordForIndex(int index) const;
    int indexForCoord(MazeCoord coord) const;
    bool isValidCoord(MazeCoord coord) const;
    void setEndpoints(int startIndex, int goalIndex);

    int startIndex() const { return startIndex_; }
    int goalIndex() const { return goalIndex_; }

private:
    std::vector<MazeCell> cells_;
    int goalIndex_ = 0;
    int height_ = 0;
    int startIndex_ = 0;
    int width_ = 0;
};

} // namespace Ui
} // namespace DirtSim
