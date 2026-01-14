#pragma once

#include "Neighborhood3x3.h"

#include <cstdint>
#include <vector>

namespace DirtSim {

/**
 * Generic bit-packed grid for tracking boolean cell properties.
 * Uses 8×8 block representation inspired by chess bitboards.
 *
 * Can track any boolean property: empty cells, active cells, etc.
 *
 * Bit mapping within each uint64_t block (row-major):
 *   Bit 0-7:   Row 0 (y=0), x increasing left to right
 *   Bit 8-15:  Row 1 (y=1)
 *   ...
 *   Bit 56-63: Row 7 (y=7)
 */
class CellBitmap {
private:
    int16_t grid_width_;
    int16_t grid_height_;
    int16_t blocks_x_; // Number of 8×8 blocks horizontally.
    int16_t blocks_y_; // Number of 8×8 blocks vertically.
    std::vector<uint64_t> blocks_;

    static constexpr int BLOCK_SIZE = 8;

    // Convert cell coordinates to block index and bit index.
    inline void cellToBlockAndBit(int x, int y, int& block_idx, int& bit_idx) const;

public:
    CellBitmap(int width, int height);

    // Core bit operations.
    void set(int x, int y);
    void clear(int x, int y);
    bool isSet(int x, int y) const;

    // Block-level operations.
    uint64_t getBlock(int block_x, int block_y) const;
    bool isBlockAllSet(int block_x, int block_y) const;   // All bits = 1.
    bool isBlockAllClear(int block_x, int block_y) const; // All bits = 0.

    // Neighborhood extraction.
    Neighborhood3x3 getNeighborhood3x3(int x, int y) const;

    // Grid dimensions.
    int getWidth() const { return grid_width_; }
    int getHeight() const { return grid_height_; }
    int getBlocksX() const { return blocks_x_; }
    int getBlocksY() const { return blocks_y_; }
};

} // namespace DirtSim
