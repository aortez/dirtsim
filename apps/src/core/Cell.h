#pragma once

#include "MaterialType.h"
#include "Vector2.h"
#include "Vector2d.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace DirtSim {

/**
 * \file
 * Cell represents a single cell in the World pure-material physics system.
 * Unlike Cell (mixed dirt/water), Cell contains a single material type with
 * a fill ratio [0,1] indicating how much of the cell is occupied.
 *
 * Note: Direct member access is now public. Use helper methods when invariants matter.
 */

struct Cell {
    // Material fill threshold constants.
    static constexpr float MIN_FILL_THRESHOLD = 0.001f; // Minimum matter to consider
    static constexpr float MAX_FILL_THRESHOLD = 0.999f; // Maximum fill before "full"

    // COM bounds.
    static constexpr float COM_MIN = -1.0f;
    static constexpr float COM_MAX = 1.0f;

    // Cell rendering dimensions (pixels).
    static constexpr uint32_t WIDTH = 30;
    static constexpr uint32_t HEIGHT = 30;

    MaterialType material_type = MaterialType::AIR;
    float fill_ratio = 0.0f;
    Vector2f com = {};
    Vector2f velocity = {};

    // Unified pressure system.
    float pressure = 0.0f;
    Vector2f pressure_gradient = {};

    // Physics force accumulation.
    Vector2f pending_force = {};

    // Rendering override: -1 = use material_type, 0+ = MaterialType to render as.
    // Allows cells to behave as one material but display as another.
    int8_t render_as = -1;

    // Calculated lit color (packed RGBA).
    uint32_t color_ = 0x000000FF;

    const MaterialProperties& material() const;

    // Helper with invariant: clamps fill ratio and auto-converts to AIR.
    void setFillRatio(float ratio);

    // Helper to add and clear pending forces.
    void addPendingForce(const Vector2f& force);
    void clearPendingForce();

    // Convenience queries.
    bool isEmpty() const;
    bool isFull() const;
    bool isAir() const;
    bool isWall() const;

    // Get the material type to use for rendering (respects render_as override).
    MaterialType getRenderMaterial() const;

    // Get the calculated lit color (packed RGBA).
    uint32_t getColor() const { return color_; }

    // Set the calculated lit color (packed RGBA).
    void setColor(uint32_t color) { color_ = color; }

    // Center of mass position [-1,1] within cell (has clamping logic).
    void setCOM(const Vector2f& com);
    void setCOM(float x, float y);

    void clearPressure();

    // Available capacity for more material.
    double getCapacity() const;

    double getMass() const;

    double getEffectiveDensity() const;

    // Add material to this cell (returns amount actually added)
    double addMaterial(MaterialType type, double amount);

    // Add material with physics context for realistic COM placement
    double addMaterialWithPhysics(
        MaterialType type,
        double amount,
        const Vector2d& source_com,
        const Vector2d& newVel,
        const Vector2d& boundary_normal);

    // Remove material from this cell (returns amount actually removed)
    double removeMaterial(double amount);

    // Transfer material to another cell (returns amount transferred)
    double transferTo(Cell& target, double amount);

    // Physics-aware transfer with boundary crossing information
    double transferToWithPhysics(Cell& target, double amount, const Vector2d& boundary_normal);

    // Replace all material with new type and amount
    void replaceMaterial(MaterialType type, double fill_ratio = 1.0);

    // Clear cell (set to empty air)
    void clear();

    void clampCOM();

    // Check if COM indicates transfer should occur
    bool shouldTransfer() const;

    // Get transfer direction based on COM position
    Vector2d getTransferDirection() const;

    // Basic material addition
    void addDirt(double amount);
    void addWater(double amount);

    // Advanced material addition with physics.
    void addDirtWithVelocity(double amount, const Vector2d& newVel);
    void addDirtWithCOM(double amount, const Vector2d& newCom, const Vector2d& newVel);

    double getTotalMaterial() const;

    std::string toAsciiCharacter() const;

    // Debug string representation
    std::string toString() const;

    nlohmann::json toJson() const;
    static Cell fromJson(const nlohmann::json& json);

    // Calculate realistic landing position for transferred material.
    Vector2d calculateTrajectoryLanding(
        const Vector2d& source_com,
        const Vector2d& velocity,
        const Vector2d& boundary_normal) const;

    void updateUnifiedPressure();
};

inline void to_json(nlohmann::json& j, const Cell& cell)
{
    j = cell.toJson();
}

inline void from_json(const nlohmann::json& j, Cell& cell)
{
    cell = Cell::fromJson(j);
}

} // namespace DirtSim
