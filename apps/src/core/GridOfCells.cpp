#include "GridOfCells.h"

#include "ScopeTimer.h"
#include "spdlog/spdlog.h"

#include <algorithm>

namespace DirtSim {

// Runtime toggle for cache usage (default: enabled).
bool GridOfCells::USE_CACHE = true;

// Runtime toggle for OpenMP parallelization (default: enabled).
bool GridOfCells::USE_OPENMP = true;

GridOfCells::GridOfCells(
    std::vector<Cell>& cells, std::vector<CellDebug>& debug_info, int width, int height)
    : cells_(cells),
      debug_info_(debug_info),
      empty_cells_(width, height),
      wall_cells_(width, height),
      empty_neighborhoods_(static_cast<size_t>(width * height), 0),
      material_neighborhoods_(static_cast<size_t>(width * height), 0),
      width_(static_cast<int16_t>(width)),
      height_(static_cast<int16_t>(height))
{
    spdlog::debug("GridOfCells: Constructing cache ({}x{})", width, height);
    populateAll();
    spdlog::debug("GridOfCells: Construction complete");
}

void GridOfCells::populateMaps()
{
    // Single pass over all cells to build all bitmaps.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Cell& cell = cells_[y * width_ + x];

            // Build empty cell bitmap.
            if (cell.isEmpty()) {
                empty_cells_.set(x, y);
            }

            // Build wall cell bitmap.
            if (cell.isWall()) {
                wall_cells_.set(x, y);
            }
        }
    }
}

void GridOfCells::buildEmptyCellMap()
{
    // Legacy method - kept for compatibility.
    // Consider removing if not called directly.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Cell& cell = cells_[y * width_ + x];

            if (cell.isEmpty()) {
                empty_cells_.set(x, y);
            }
        }
    }
}

void GridOfCells::buildWallCellMap()
{
    // Scan all cells and mark walls in bitmap.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Cell& cell = cells_[y * width_ + x];

            if (cell.isWall()) {
                wall_cells_.set(x, y);
            }
        }
    }
}

void GridOfCells::precomputeEmptyNeighborhoods()
{
    // Precompute 3×3 neighborhood for every cell.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            Neighborhood3x3 n = empty_cells_.getNeighborhood3x3(x, y);
            empty_neighborhoods_[y * width_ + x] = n.data;
        }
    }
}

void GridOfCells::precomputeMaterialNeighborhoods()
{
    // Precompute 3×3 material neighborhood for every cell.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            uint64_t packed = 0;

            // Pack 9 material types (4 bits each) into uint64_t.
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int bit_group = (dy + 1) * 3 + (dx + 1); // 0-8
                    int nx = x + dx;
                    int ny = y + dy;

                    Material::EnumType mat = Material::EnumType::Air; // Default for OOB.
                    if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_) {
                        const Cell& cell = cells_[ny * width_ + nx];
                        mat = cell.material_type;
                    }

                    // Pack material type (4 bits) into position.
                    uint64_t mat_bits = static_cast<uint64_t>(mat) & 0xF;
                    packed |= (mat_bits << (bit_group * 4));
                }
            }

            material_neighborhoods_[y * width_ + x] = packed;
        }
    }
}

void GridOfCells::populateAll()
{
    // Single-pass: build all caches in one grid scan.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int idx = y * width_ + x;
            const Cell& cell = cells_[idx];

            if (cell.isEmpty()) {
                empty_cells_.set(x, y);
            }

            if (cell.isWall()) {
                wall_cells_.set(x, y);
            }

            // Build 3x3 neighborhoods from cell data directly.
            uint64_t empty_packed = 0;
            uint64_t mat_packed = 0;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int bit_pos = (dy + 1) * 3 + (dx + 1);
                    const int nx = x + dx;
                    const int ny = y + dy;

                    if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_) {
                        const Cell& neighbor = cells_[ny * width_ + nx];

                        // Empty neighborhood: validity + value.
                        empty_packed |= (1ULL << (9 + bit_pos));
                        if (neighbor.isEmpty()) {
                            empty_packed |= (1ULL << bit_pos);
                        }

                        // Material neighborhood: 4-bit material type.
                        uint64_t mat_bits = static_cast<uint64_t>(neighbor.material_type) & 0xF;
                        mat_packed |= (mat_bits << (bit_pos * 4));
                    }
                }
            }

            empty_neighborhoods_[idx] = empty_packed;
            material_neighborhoods_[idx] = mat_packed;
        }
    }
}

void GridOfCells::rebuildSeparatePasses()
{
    // Reset caches.
    empty_cells_ = CellBitmap(width_, height_);
    wall_cells_ = CellBitmap(width_, height_);
    std::fill(empty_neighborhoods_.begin(), empty_neighborhoods_.end(), 0ULL);
    std::fill(material_neighborhoods_.begin(), material_neighborhoods_.end(), 0ULL);

    // Rebuild using the original three-pass approach.
    populateMaps();
    precomputeEmptyNeighborhoods();
    precomputeMaterialNeighborhoods();
}

} // namespace DirtSim
