#include "WorldViscosityCalculator.h"
#include "Cell.h"
#include "GridOfCells.h"
#include "PhysicsSettings.h"
#include "World.h"
#include "WorldData.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

WorldViscosityCalculator::ViscousForce WorldViscosityCalculator::calculateViscousForce(
    const World& world,
    uint32_t x,
    uint32_t y,
    double viscosity_strength,
    const GridOfCells* /*grid*/) const
{
    const WorldData& data = world.getData();
    const Cell& cell = data.at(x, y);

    // Skip empty cells and walls.
    if (cell.isEmpty() || cell.isWall()) {
        return ViscousForce{ .force = { 0.0, 0.0 },
                             .neighbor_avg_speed = 0.0,
                             .neighbor_count = 0 };
    }

    // Get material properties.
    const MaterialProperties& props = getMaterialProperties(cell.material_type);

    // Skip if viscosity is zero.
    if (props.viscosity <= 0.0) {
        return ViscousForce{ .force = { 0.0, 0.0 },
                             .neighbor_avg_speed = 0.0,
                             .neighbor_count = 0 };
    }

    // Calculate weighted average velocity of same-material neighbors.
    Vector2d velocity_sum{ 0.0, 0.0 };
    double weight_sum = 0.0;
    int neighbor_count = 0;

    // Check all 8 neighbors.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }

            int nx = static_cast<int>(x) + dx;
            int ny = static_cast<int>(y) + dy;

            // Bounds check.
            if (nx < 0 || ny < 0 || nx >= static_cast<int>(data.width)
                || ny >= static_cast<int>(data.height)) {
                continue;
            }

            const Cell& neighbor = data.at(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));

            // Only couple with same-material neighbors.
            if (neighbor.material_type != cell.material_type || neighbor.isEmpty()) {
                continue;
            }

            neighbor_count++;

            // Distance weighting (diagonal neighbors are farther).
            double distance_weight = (dx != 0 && dy != 0) ? 0.707 : 1.0;

            // Fill ratio weighting (more matter = stronger influence).
            double fill_weight = neighbor.fill_ratio;

            // Combined weight.
            double weight = distance_weight * fill_weight;

            velocity_sum += neighbor.velocity * weight;
            weight_sum += weight;
        }
    }

    // No neighbors = no viscous coupling.
    if (neighbor_count == 0) {
        return ViscousForce{ .force = { 0.0, 0.0 },
                             .neighbor_avg_speed = 0.0,
                             .neighbor_count = 0 };
    }

    // Calculate average neighbor velocity.
    Vector2d avg_neighbor_velocity =
        (weight_sum > 0.0) ? (velocity_sum / weight_sum) : Vector2d{ 0.0, 0.0 };

    // Velocity difference drives viscous force.
    Vector2d velocity_difference = avg_neighbor_velocity - cell.velocity;

    // Scale viscosity by connectivity (isolated particles experience less viscous drag).
    double connectivity_factor = static_cast<double>(neighbor_count) / 8.0;
    double effective_viscosity = props.viscosity * connectivity_factor;

    // Viscous force tries to eliminate velocity differences.
    // Scale by viscosity strength (UI control) and fill ratio.
    Vector2d viscous_force =
        velocity_difference * effective_viscosity * viscosity_strength * cell.fill_ratio;

    // Debug info.
    double neighbor_avg_speed = avg_neighbor_velocity.magnitude();

    return ViscousForce{ .force = viscous_force,
                         .neighbor_avg_speed = neighbor_avg_speed,
                         .neighbor_count = neighbor_count };
}

} // namespace DirtSim
