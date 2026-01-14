#include "WorldCohesionCalculator.h"
#include "Cell.h"
#include "GridOfCells.h"
#include "MaterialType.h"
#include "World.h"
#include "WorldData.h"
#include "bitmaps/MaterialNeighborhood.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cmath>

using namespace DirtSim;

WorldCohesionCalculator::CohesionForce WorldCohesionCalculator::calculateCohesionForce(
    const World& world, int x, int y) const
{
    const auto& data = world.getData();
    const Cell& cell = data.at(x, y);
    // Skip AIR cells - they have zero cohesion and don't participate in clustering.
    if (cell.material_type == Material::EnumType::Air) {
        return { 0.0f, 0 };
    }

    const Material::Properties& props = Material::getProperties(cell.material_type);
    const float material_cohesion = props.cohesion;
    int connected_neighbors = 0; // Accumulated in loop.

    // Check 4 cardinal neighbors only.
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue; // Skip self.
            if (dx != 0 && dy != 0) continue; // Skip diagonals - only cardinal directions.

            const int nx = x + dx;
            const int ny = y + dy;

            if (data.inBounds(nx, ny)) {
                const Cell& neighbor = data.at(nx, ny);

                // Count same-material neighbors.
                if (neighbor.material_type == cell.material_type
                    && neighbor.fill_ratio > MIN_MATTER_THRESHOLD) {
                    connected_neighbors += 1;
                }
            }
        }
    }

    // Check for metal neighbors that provide structural support.
    int metal_neighbors = 0; // Accumulated in loop.
    if (cell.material_type == Material::EnumType::Metal) {
        // Count metal neighbors for structural support.
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue; // Skip self.
                if (dx != 0 && dy != 0) continue; // Skip diagonals - only cardinal directions.

                const int nx = x + dx;
                const int ny = y + dy;

                if (data.inBounds(nx, ny)) {
                    const Cell& neighbor = data.at(nx, ny);
                    if (neighbor.material_type == Material::EnumType::Metal
                        && neighbor.fill_ratio > 0.5f) {
                        metal_neighbors++;
                    }
                }
            }
        }
    }

    // EXPERIMENT: Simplified cohesion - no support modulation.
    // Test if full-strength cohesion + friction can create stable structures without explicit
    // support.

    // Resistance magnitude = cohesion × connection strength × own fill ratio.
    // (Removed support_factor - let cohesion work at full strength always)
    const float resistance = material_cohesion * connected_neighbors * cell.fill_ratio;

    spdlog::trace(
        "Cohesion calculation for {} at ({},{}): neighbors={}, resistance={:.3f}",
        toString(cell.material_type),
        x,
        y,
        connected_neighbors,
        resistance);

    return { resistance, connected_neighbors };
}

WorldCohesionCalculator::COMCohesionForce WorldCohesionCalculator::calculateCOMCohesionForce(
    const World& world, int x, int y, int com_cohesion_range, const GridOfCells* grid) const
{
    const auto& data = world.getData();

    // Use cache-optimized path if available.
    if (GridOfCells::USE_CACHE && grid) {
        const MaterialNeighborhood mat_n = grid->getMaterialNeighborhood(x, y);
        return calculateCOMCohesionForceCached(world, x, y, com_cohesion_range, mat_n);
    }

    // Fallback to direct cell access.
    const Cell& cell = data.at(x, y);
    // Skip AIR cells - they have zero cohesion and don't participate in clustering.
    if (cell.material_type == Material::EnumType::Air) {
        return { { 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f }, 0, 0.0f, 0.0f, false, 0.0f };
    }

    // Tunable force balance (adjust these to tune clustering vs stability).
    // Centering is primary (keeps COMs stable), clustering is secondary (gentle aggregation).
    static constexpr float CLUSTERING_WEIGHT = 0.5f; // Weak neighbor attraction (dampening).
    static constexpr float CENTERING_WEIGHT = 1.0f;  // Strong COM centering (stability).

    const float cell_mass = cell.getMass();
    const Vector2f com = cell.com;
    const Vector2f cell_world_pos(
        static_cast<float>(x) + cell.com.x, static_cast<float>(y) + cell.com.y);
    const Material::Properties& props = Material::getProperties(cell.material_type);

    // ===================================================================
    // FORCE 1: Clustering (attraction toward same-material neighbors)
    // ===================================================================

    Vector2f neighbor_center_sum(0.0f, 0.0f);
    float total_weight = 0.0f;
    int connection_count = 0;

    for (int dx = -com_cohesion_range; dx <= com_cohesion_range; dx++) {
        for (int dy = -com_cohesion_range; dy <= com_cohesion_range; dy++) {
            if (dx == 0 && dy == 0) continue;
            if (dx != 0 && dy != 0) continue; // Skip diagonals - only cardinal directions.

            const int nx = x + dx;
            const int ny = y + dy;

            if (data.inBounds(nx, ny)) {
                const Cell& neighbor = data.at(nx, ny);

                // Count same-material neighbors.
                if (neighbor.material_type == cell.material_type
                    && neighbor.fill_ratio > MIN_MATTER_THRESHOLD) {
                    const Vector2f neighbor_world_pos(
                        static_cast<float>(nx) + neighbor.com.x,
                        static_cast<float>(ny) + neighbor.com.y);
                    const float weight = neighbor.fill_ratio;

                    neighbor_center_sum += neighbor_world_pos * weight;
                    total_weight += weight;
                    connection_count++;
                }
            }
        }
    }

    Vector2f clustering_force(0.0f, 0.0f);
    Vector2f neighbor_center(0.0f, 0.0f);

    if (connection_count > 0 && total_weight > MIN_MATTER_THRESHOLD) {
        neighbor_center = neighbor_center_sum / total_weight;
        const Vector2f to_neighbors = neighbor_center - cell_world_pos;
        const float distance_sq = to_neighbors.x * to_neighbors.x + to_neighbors.y * to_neighbors.y;

        if (distance_sq > 0.000001f) {
            const float distance = std::sqrt(distance_sq);
            const Vector2f clustering_direction = to_neighbors * (1.0f / distance);

            const float distance_factor = 1.0f / (distance + 0.1f);
            const float max_connections =
                static_cast<float>((2 * com_cohesion_range + 1) * (2 * com_cohesion_range + 1) - 1);

            // Mass-based factor: Uses total neighbor fill ratios (not just count).
            // This makes larger/fuller clusters pull harder than sparse ones.
            const float mass_factor = total_weight / max_connections;

            float clustering_magnitude =
                props.cohesion * mass_factor * distance_factor * cell.fill_ratio;

            // Cap to prevent excessive forces.
            const float max_clustering = props.cohesion * 10.0f;
            clustering_magnitude = std::min(clustering_magnitude, max_clustering);

            clustering_force = clustering_direction * clustering_magnitude * CLUSTERING_WEIGHT;
        }
    }

    // ===================================================================
    // FORCE 2: Centering (scaled by neighbor connectivity)
    // ===================================================================

    Vector2f centering_force(0.0f, 0.0f);
    Vector2f centering_direction(0.0f, 0.0f);
    const float com_offset_sq = com.x * com.x + com.y * com.y;
    float com_offset = 0.0f;

    // Only apply centering when particle has same-material neighbors.
    // Isolated particles should move freely without artificial COM drag.
    if (connection_count > 0 && com_offset_sq > 0.000001f) {
        com_offset = std::sqrt(com_offset_sq);
        centering_direction = com * (-1.0f / com_offset);

        // Scale by neighbor connectivity - more neighbors = stronger centering.
        const float max_connections =
            static_cast<float>((2 * com_cohesion_range + 1) * (2 * com_cohesion_range + 1) - 1);
        const float connection_factor = static_cast<float>(connection_count) / max_connections;

        const float centering_magnitude =
            props.cohesion * com_offset * cell.fill_ratio * connection_factor;

        // EXPERIMENT: Removed support-based centering boost.
        // Let natural cohesion handle centering without explicit support checks.

        centering_force = centering_direction * centering_magnitude * CENTERING_WEIGHT;
    }

    Vector2f final_force = centering_force; // Modified by clustering.

    const float clustering_force_sq =
        clustering_force.x * clustering_force.x + clustering_force.y * clustering_force.y;
    if (clustering_force_sq > 0.000001f && com_offset_sq > 0.000001f) {
        const Vector2f cell_grid_pos(static_cast<float>(x), static_cast<float>(y));
        const Vector2f to_neighbors_vec = neighbor_center - cell_grid_pos;
        const float to_neighbors_mag_sq =
            to_neighbors_vec.x * to_neighbors_vec.x + to_neighbors_vec.y * to_neighbors_vec.y;
        const Vector2f to_neighbors = to_neighbors_vec * (1.0f / std::sqrt(to_neighbors_mag_sq));

        const float alignment = to_neighbors.dot(centering_direction);

        spdlog::trace(
            "Alignment check at ({},{}): to_neighbors=({:.3f},{:.3f}), to_center=({:.3f},{:.3f}), "
            "alignment={:.3f}",
            x,
            y,
            to_neighbors.x,
            to_neighbors.y,
            centering_direction.x,
            centering_direction.y,
            alignment);

        if (alignment > 0.0f) {
            // Clustering helps centering → apply it (weighted by alignment strength).
            final_force = final_force + clustering_force * alignment;
            spdlog::trace(
                "Clustering APPLIED (alignment={:.3f}): boost=({:.4f},{:.4f})",
                alignment,
                (clustering_force * alignment).x,
                (clustering_force * alignment).y);
        }
        else {
            spdlog::trace("Clustering SKIPPED (alignment={:.3f} <= 0)", alignment);
        }
    }

    const float total_force_magnitude =
        std::sqrt(final_force.x * final_force.x + final_force.y * final_force.y);

    spdlog::trace(
        "Dual cohesion for {} at ({},{}): connections={}, com_offset={:.3f}, "
        "clustering=({:.3f},{:.3f}), centering=({:.3f},{:.3f}), total_mag={:.3f}",
        toString(cell.material_type),
        x,
        y,
        connection_count,
        com_offset,
        clustering_force.x,
        clustering_force.y,
        centering_force.x,
        centering_force.y,
        total_force_magnitude);

    // EXPERIMENT: Calculate resistance without support factor.
    const float resistance = props.cohesion * connection_count * cell.fill_ratio;

    return { final_force,
             total_force_magnitude,
             neighbor_center,
             connection_count,
             0.0f,
             cell_mass,
             (connection_count > 0 || com_offset_sq > 0.000001f),
             resistance };
}

WorldCohesionCalculator::COMCohesionForce WorldCohesionCalculator::calculateCOMCohesionForceCached(
    const World& world,
    int x,
    int y,
    int com_cohesion_range,
    const MaterialNeighborhood& mat_n) const
{
    const auto& data = world.getData();
    const Cell& cell = data.at(x, y);
    // Skip AIR cells - they have zero cohesion and don't participate in clustering.
    if (cell.material_type == Material::EnumType::Air) {
        return { { 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f }, 0, 0.0f, 0.0f, false, 0.0f };
    }

    // Tunable force balance (same as non-cached version).
    static constexpr float CLUSTERING_WEIGHT = 0.5f;
    static constexpr float CENTERING_WEIGHT = 1.0f;

    const float cell_mass = cell.getMass();
    const Vector2f com = cell.com;
    const Vector2f cell_world_pos(
        static_cast<float>(x) + cell.com.x, static_cast<float>(y) + cell.com.y);
    const Material::Properties& props = Material::getProperties(cell.material_type);

    // Get center material from cache.
    const Material::EnumType my_material = mat_n.getCenterMaterial();

    // ===================================================================
    // FORCE 1: Clustering (cache-optimized - use MaterialNeighborhood)
    // ===================================================================

    Vector2f neighbor_center_sum(0.0f, 0.0f);
    float total_weight = 0.0f;
    int connection_count = 0;

    for (int dx = -com_cohesion_range; dx <= com_cohesion_range; dx++) {
        for (int dy = -com_cohesion_range; dy <= com_cohesion_range; dy++) {
            if (dx == 0 && dy == 0) continue;
            if (dx != 0 && dy != 0) continue; // Cardinals only.

            const int nx = x + dx;
            const int ny = y + dy;

            // Explicit bounds check - skip out-of-bounds neighbors.
            if (!data.inBounds(nx, ny)) {
                continue;
            }

            // Multi-stage cache filtering (bounds check handled by cache).
            // Stage 1: Material match check (pure cache - no cell access).
            const bool is_same_material = mat_n.getMaterial(dx, dy) == my_material;

            if (!is_same_material) continue;

            // At this point: same material, guaranteed non-empty.
            // Fetch cell for physics calculations.
            const Cell& neighbor = data.at(nx, ny);

            const Vector2f neighbor_world_pos(
                static_cast<float>(nx) + neighbor.com.x, static_cast<float>(ny) + neighbor.com.y);
            const float weight = neighbor.fill_ratio;
            neighbor_center_sum += neighbor_world_pos * weight;
            total_weight += weight;
            connection_count++;
        }
    }

    // Calculate clustering force (same logic as non-cached version).
    Vector2f clustering_force(0.0f, 0.0f);
    Vector2f neighbor_center(0.0f, 0.0f);

    if (connection_count > 0 && total_weight > MIN_MATTER_THRESHOLD) {
        neighbor_center = neighbor_center_sum / total_weight;
        const Vector2f to_neighbors = neighbor_center - cell_world_pos;
        const float distance_sq = to_neighbors.x * to_neighbors.x + to_neighbors.y * to_neighbors.y;

        if (distance_sq > 0.000001f) {
            const float distance = std::sqrt(distance_sq);
            const Vector2f clustering_direction = to_neighbors * (1.0f / distance);

            const float distance_factor = 1.0f / (distance + 0.1f);
            const float max_connections =
                static_cast<float>((2 * com_cohesion_range + 1) * (2 * com_cohesion_range + 1) - 1);

            const float mass_factor = total_weight / max_connections;
            float clustering_magnitude =
                props.cohesion * mass_factor * distance_factor * cell.fill_ratio;

            const float max_clustering = props.cohesion * 10.0f;
            clustering_magnitude = std::min(clustering_magnitude, max_clustering);
            clustering_force = clustering_direction * clustering_magnitude * CLUSTERING_WEIGHT;
        }
    }

    // ===================================================================
    // FORCE 2: Centering (scaled by neighbor connectivity)
    // ===================================================================

    Vector2f centering_force(0.0f, 0.0f);
    Vector2f centering_direction(0.0f, 0.0f);
    const float com_offset_sq = com.x * com.x + com.y * com.y;
    float com_offset = 0.0f;

    // Only apply centering when particle has same-material neighbors.
    // Isolated particles should move freely without artificial COM drag.
    if (connection_count > 0 && com_offset_sq > 0.000001f) {
        com_offset = std::sqrt(com_offset_sq);
        centering_direction = com * (-1.0f / com_offset);

        // Scale by neighbor connectivity - more neighbors = stronger centering.
        const float max_connections =
            static_cast<float>((2 * com_cohesion_range + 1) * (2 * com_cohesion_range + 1) - 1);
        const float connection_factor = static_cast<float>(connection_count) / max_connections;

        const float centering_magnitude =
            props.cohesion * com_offset * cell.fill_ratio * connection_factor;
        centering_force = centering_direction * centering_magnitude * CENTERING_WEIGHT;
    }

    // Combine forces with alignment check (matching non-cached version).
    Vector2f final_force = centering_force; // Modified by clustering.

    const float clustering_force_sq =
        clustering_force.x * clustering_force.x + clustering_force.y * clustering_force.y;
    if (clustering_force_sq > 0.000001f && com_offset_sq > 0.000001f) {
        const Vector2f cell_grid_pos(static_cast<float>(x), static_cast<float>(y));
        const Vector2f to_neighbors_vec = neighbor_center - cell_grid_pos;
        const float to_neighbors_mag_sq =
            to_neighbors_vec.x * to_neighbors_vec.x + to_neighbors_vec.y * to_neighbors_vec.y;
        const Vector2f to_neighbors = to_neighbors_vec * (1.0f / std::sqrt(to_neighbors_mag_sq));

        const float alignment = to_neighbors.dot(centering_direction);
        if (alignment > 0.0f) {
            // Clustering helps centering → apply it (weighted by alignment strength).
            final_force = final_force + clustering_force * alignment;
        }
    }

    const float total_force_magnitude =
        std::sqrt(final_force.x * final_force.x + final_force.y * final_force.y);

    // EXPERIMENT: Calculate resistance without support factor (cached path).
    const float resistance = props.cohesion * connection_count * cell.fill_ratio;

    return { final_force,
             total_force_magnitude,
             neighbor_center,
             connection_count,
             total_weight,
             cell_mass,
             (connection_count > 0 || com_offset_sq > 0.000001f),
             resistance };
}
