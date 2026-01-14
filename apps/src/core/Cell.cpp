#include "Cell.h"
#include "World.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>

#include "spdlog/spdlog.h"

using namespace DirtSim;

void Cell::setFillRatio(float ratio)
{
    fill_ratio = std::clamp(ratio, 0.0f, 1.0f);

    // If fill ratio becomes effectively zero, convert to empty AIR.
    if (fill_ratio < MIN_FILL_THRESHOLD) {
        if (material_type == Material::EnumType::Wood) {
            spdlog::info(
                "Cell::setFillRatio - clearing Wood cell (fill {:.3f} -> 0.0)", fill_ratio);
        }
        material_type = Material::EnumType::Air;
        fill_ratio = 0.0f;
        velocity = Vector2f{ 0.0f, 0.0f };
        com = Vector2f{ 0.0f, 0.0f };

        // Clear pressure values when converting to air.
        pressure = 0.0f;
        pressure_gradient = Vector2f{ 0.0f, 0.0f };
    }
}

void Cell::setCOM(const Vector2f& newCom)
{
    com =
        Vector2f{ std::clamp(newCom.x, COM_MIN, COM_MAX), std::clamp(newCom.y, COM_MIN, COM_MAX) };
}

float Cell::getMass() const
{
    if (isEmpty()) {
        return 0.0f;
    }
    return fill_ratio * static_cast<float>(Material::getDensity(material_type));
}

float Cell::getEffectiveDensity() const
{
    return fill_ratio * static_cast<float>(Material::getDensity(material_type));
}

float Cell::addMaterial(Material::EnumType type, float amount)
{
    if (amount <= 0.0f) {
        return 0.0f;
    }

    // Empty cells accept any material type.
    if (isEmpty()) {
        material_type = type;
        const float added = std::min(amount, 1.0f);
        fill_ratio = added;
        return added;
    }

    // If different material type, no mixing allowed.
    if (material_type != type) {
        return 0.0f;
    }

    // Add to existing material.
    const float capacity = getCapacity();
    const float added = std::min(amount, capacity);
    fill_ratio += added;

    return added;
}

float Cell::addMaterialWithPhysics(
    Material::EnumType type,
    float amount,
    const Vector2f& source_com,
    const Vector2f& newVel,
    const Vector2f& boundary_normal)
{
    if (amount <= 0.0f) {
        return 0.0f;
    }

    // If we're empty, accept any material type with trajectory-based COM.
    if (isEmpty()) {
        if (material_type == Material::EnumType::Wood && type != Material::EnumType::Wood) {
            spdlog::info(
                "Cell::addMaterialWithPhysics - replacing Wood with {} in 'empty' cell "
                "(old_fill={:.3f})",
                Material::toString(type),
                fill_ratio);
        }
        material_type = type;
        const float added = std::min(amount, 1.0f);
        fill_ratio = added;

        // Calculate realistic landing position based on boundary crossing.
        com = calculateTrajectoryLanding(source_com, newVel, boundary_normal);
        velocity = newVel;

        return added;
    }

    // If different material type, no mixing allowed.
    if (material_type != type) {
        return 0.0f;
    }

    // Add to existing material with momentum conservation.
    const float capacity = getCapacity();
    const float added = std::min(amount, capacity);

    if (added > 0.0f) {
        // Enhanced momentum conservation: new_COM = (m1*COM1 + m2*COM2)/(m1+m2).
        const float existing_mass = getMass();
        const float added_mass = added * static_cast<float>(material().density);
        const float total_mass = existing_mass + added_mass;

        // Calculate incoming material's COM in target cell space.
        Vector2f incoming_com = calculateTrajectoryLanding(source_com, newVel, boundary_normal);

        if (total_mass > World::MIN_MATTER_THRESHOLD) {
            // Weighted average of COM positions.
            com = (com * existing_mass + incoming_com * added_mass) / total_mass;

            // Momentum conservation for velocity.
            velocity = (velocity * existing_mass + newVel * added_mass) / total_mass;
        }

        fill_ratio += added;
    }

    return added;
}

float Cell::removeMaterial(float amount)
{
    if (isEmpty() || amount <= 0.0f) {
        return 0.0f;
    }

    const float removed = std::min(amount, fill_ratio);
    fill_ratio -= removed;

    // Check if we became empty.
    if (fill_ratio < MIN_FILL_THRESHOLD) {
        clear();
    }

    return removed;
}

float Cell::transferTo(Cell& target, float amount)
{
    if (isEmpty() || amount <= 0.0f) {
        return 0.0f;
    }

    // Calculate how much we can actually transfer.
    const float available = std::min(amount, fill_ratio);
    const float accepted = target.addMaterial(material_type, available);

    // Remove the accepted amount from this cell.
    if (accepted > 0.0f) {
        removeMaterial(accepted);
    }

    return accepted;
}

float Cell::transferToWithPhysics(Cell& target, float amount, const Vector2f& boundary_normal)
{
    if (isEmpty() || amount <= 0.0f) {
        return 0.0f;
    }

    // Calculate how much we can actually transfer.
    const float available = std::min(amount, fill_ratio);

    // Use physics-aware method with current COM and velocity.
    // Note: organism tracking is handled by OrganismManager, not Cell.
    const float accepted =
        target.addMaterialWithPhysics(material_type, available, com, velocity, boundary_normal);

    if (accepted > 0.0f) {
        removeMaterial(accepted);
    }

    return accepted;
}

void Cell::replaceMaterial(Material::EnumType type, float new_fill_ratio)
{
    // Reset to default state, then set the new material.
    // This ensures all fields (render_as, pressure, pending_force, etc.) are cleared.
    *this = Cell{};
    material_type = type;
    setFillRatio(new_fill_ratio);
}

void Cell::clear()
{
    *this = Cell{};
}

void Cell::clampCOM()
{
    com.x = std::clamp(com.x, COM_MIN, COM_MAX);
    com.y = std::clamp(com.y, COM_MIN, COM_MAX);
}

bool Cell::shouldTransfer() const
{
    if (isEmpty() || isWall()) {
        return false;
    }

    // Transfer only when COM reaches cell boundaries (±1.0) per GridMechanics.md.
    return std::abs(com.x) >= 1.0 || std::abs(com.y) >= 1.0;
}

Vector2f Cell::getTransferDirection() const
{
    // Determine primary transfer direction based on COM position at boundaries.
    Vector2f direction(0.0f, 0.0f);

    if (com.x >= 1.0f) {
        direction.x = 1.0f; // Transfer right when COM reaches right boundary.
    }
    else if (com.x <= -1.0f) {
        direction.x = -1.0f; // Transfer left when COM reaches left boundary.
    }

    if (com.y >= 1.0f) {
        direction.y = 1.0f; // Transfer down when COM reaches bottom boundary.
    }
    else if (com.y <= -1.0f) {
        direction.y = -1.0f; // Transfer up when COM reaches top boundary.
    }

    return direction;
}

Vector2f Cell::calculateTrajectoryLanding(
    const Vector2f& source_com, const Vector2f& velocity, const Vector2f& boundary_normal) const
{
    // Calculate where material actually crosses the boundary.
    Vector2f boundary_crossing_point = source_com;

    // Determine which boundary was crossed and calculate intersection.
    if (std::abs(boundary_normal.x) > 0.5f) {
        // Crossing left/right boundary.
        float boundary_x = (boundary_normal.x > 0) ? 1.0f : -1.0f;
        float crossing_ratio = (boundary_x - source_com.x) / velocity.x;
        if (std::abs(velocity.x) > 1e-6f) {
            boundary_crossing_point.x = boundary_x;
            boundary_crossing_point.y = source_com.y + velocity.y * crossing_ratio;
        }
    }
    else if (std::abs(boundary_normal.y) > 0.5f) {
        // Crossing top/bottom boundary.
        float boundary_y = (boundary_normal.y > 0) ? 1.0f : -1.0f;
        float crossing_ratio = (boundary_y - source_com.y) / velocity.y;
        if (std::abs(velocity.y) > 1e-6f) {
            boundary_crossing_point.y = boundary_y;
            boundary_crossing_point.x = source_com.x + velocity.x * crossing_ratio;
        }
    }

    // Transform crossing point to target cell coordinate space.
    Vector2f target_com = boundary_crossing_point;

    // Wrap coordinates across boundary.
    // Use 0.99 instead of 1.0 to avoid immediate re-crossing on next frame.
    constexpr float BOUNDARY_EPSILON = 0.99f;
    if (std::abs(boundary_normal.x) > 0.5f) {
        // Material crossed left/right - wrap X coordinate.
        target_com.x = (boundary_normal.x > 0) ? -BOUNDARY_EPSILON : BOUNDARY_EPSILON;
    }
    if (std::abs(boundary_normal.y) > 0.5f) {
        // Material crossed top/bottom - wrap Y coordinate.
        // DOWN (y > 0): appear at top edge (-0.99), UP (y < 0): appear at bottom edge (0.99).
        target_com.y = (boundary_normal.y > 0) ? -BOUNDARY_EPSILON : BOUNDARY_EPSILON;
    }

    // Clamp to valid COM bounds.
    target_com.x = std::clamp(target_com.x, COM_MIN, COM_MAX);
    target_com.y = std::clamp(target_com.y, COM_MIN, COM_MAX);

    return target_com;
}

std::string Cell::toString() const
{
    std::ostringstream oss;
    oss << Material::toString(material_type) << "(fill=" << fill_ratio << ", com=[" << com.x << ","
        << com.y << "]" << ", vel=[" << velocity.x << "," << velocity.y << "]" << ")";
    return oss.str();
}

// =================================================================.
// CELLINTERFACE IMPLEMENTATION.
// =================================================================.

void Cell::addDirt(float amount)
{
    if (amount <= 0.0f) return;
    addMaterial(Material::EnumType::Dirt, amount);
}

void Cell::addWater(float amount)
{
    if (amount <= 0.0f) return;
    addMaterial(Material::EnumType::Water, amount);
}

void Cell::addDirtWithVelocity(float amount, const Vector2f& newVel)
{
    if (amount <= 0.0f) return;

    // Store current fill ratio to calculate momentum.
    float oldFill = fill_ratio;
    float actualAdded = addMaterial(Material::EnumType::Dirt, amount);

    if (actualAdded > 0.0f) {
        // Update velocity based on momentum conservation.
        float newFill = fill_ratio;
        if (newFill > 0.0f) {
            // Weighted average of existing velocity and new velocity.
            velocity = (velocity * oldFill + newVel * actualAdded) / newFill;
        }
        else {
            velocity = newVel;
        }
    }
}

void Cell::addDirtWithCOM(float amount, const Vector2f& newCom, const Vector2f& newVel)
{
    if (amount <= 0.0f) return;

    // Store current state to calculate weighted averages.
    float oldFill = fill_ratio;
    Vector2f oldCOM = com;
    Vector2f oldVelocity = velocity;

    float actualAdded = addMaterial(Material::EnumType::Dirt, amount);

    if (actualAdded > 0.0f) {
        float newFill = fill_ratio;
        if (newFill > 0.0f) {
            // Weighted average of existing COM and new COM.
            com = (oldCOM * oldFill + newCom * actualAdded) / newFill;
            clampCOM(); // Ensure COM stays in bounds.

            // Weighted average of existing velocity and new velocity.
            velocity = (oldVelocity * oldFill + newVel * actualAdded) / newFill;
        }
        else {
            com = newCom;
            velocity = newVel;
        }
    }
}

float Cell::getTotalMaterial() const
{
    return fill_ratio;
}

// =================================================================.
// RENDERING METHODS.
// =================================================================.

std::string Cell::toAsciiCharacter() const
{
    if (isEmpty()) {
        return "  "; // Two spaces for empty cells (2x1 format).
    }

    // Choose character based on material type.
    char material_char = '?';
    switch (material_type) {
        case Material::EnumType::Air:
            return "  "; // Two spaces for air.
        case Material::EnumType::Dirt:
            material_char = '#';
            break;
        case Material::EnumType::Water:
            material_char = '~';
            break;
        case Material::EnumType::Wood:
            material_char = 'W';
            break;
        case Material::EnumType::Sand:
            material_char = '.';
            break;
        case Material::EnumType::Metal:
            material_char = 'M';
            break;
        case Material::EnumType::Leaf:
            material_char = 'L';
            break;
        case Material::EnumType::Wall:
            material_char = '|';
            break;
        case Material::EnumType::Root:
            material_char = 'R';
            break;
        case Material::EnumType::Seed:
            material_char = 'S';
            break;
    }

    // Convert fill ratio to 0-9 scale.
    int fill_level = static_cast<int>(std::round(fill_ratio * 9.0));
    fill_level = std::clamp(fill_level, 0, 9);

    // Return 2-character representation: material + fill level.
    return std::string(1, material_char) + std::to_string(fill_level);
}

// =================================================================
// INLINE METHOD IMPLEMENTATIONS (moved from header)
// =================================================================

const Material::Properties& Cell::material() const
{
    return Material::getProperties(material_type);
}

void Cell::addPendingForce(const Vector2f& force)
{
    pending_force = pending_force + force;
}

void Cell::clearPendingForce()
{
    pending_force = {};
}

bool Cell::isEmpty() const
{
    return fill_ratio < MIN_FILL_THRESHOLD;
}

bool Cell::isFull() const
{
    return fill_ratio > MAX_FILL_THRESHOLD;
}

bool Cell::isAir() const
{
    return material_type == Material::EnumType::Air;
}

bool Cell::isWall() const
{
    return material_type == Material::EnumType::Wall;
}

Material::EnumType Cell::getRenderMaterial() const
{
    if (render_as >= 0) {
        return static_cast<Material::EnumType>(render_as);
    }
    return material_type;
}

void Cell::setCOM(float x, float y)
{
    setCOM(Vector2f{ x, y });
}

void Cell::clearPressure()
{
    pressure = 0.0f;
}

float Cell::getCapacity() const
{
    return 1.0f - fill_ratio;
}

// =================================================================
// JSON SERIALIZATION
// =================================================================

#include "ReflectSerializer.h"

nlohmann::json Cell::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Cell Cell::fromJson(const nlohmann::json& json)
{
    return ReflectSerializer::from_json<Cell>(json);
}
