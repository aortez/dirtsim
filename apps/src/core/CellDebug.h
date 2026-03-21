#pragma once

#include "ReflectSerializer.h"
#include "Vector2d.h"
#include <nlohmann/json.hpp>

namespace DirtSim {

/**
 * CellDebug: Debug information for visualization/analysis.
 *
 * Stored separately from Cell to keep core physics data compact.
 * Owned by WorldData, referenced by GridOfCells for fast access.
 */
struct CellDebug {
    enum DirectionMask : uint8_t {
        DirectionNone = 0,
        DirectionLeft = 1 << 0,
        DirectionRight = 1 << 1,
        DirectionUp = 1 << 2,
        DirectionDown = 1 << 3,
    };

    enum CompressionBranchMask : uint8_t {
        CompressionBranchNone = 0,
        CompressionBranchGranular = 1 << 0,
        CompressionBranchSupport = 1 << 1,
    };

    // Force accumulation for visualization.
    Vector2d accumulated_viscous_force = {};
    Vector2d accumulated_adhesion_force = {};
    Vector2d accumulated_com_cohesion_force = {};
    Vector2d accumulated_friction_force = {};
    Vector2d accumulated_gravity_force = {};
    Vector2d accumulated_pressure_force = {};
    Vector2d strongest_friction_contact_force = {};
    Vector2d strongest_friction_contact_normal = {};

    // Physics debug values.
    double damping_factor = 1.0;              // Effective damping applied to velocity.
    double cohesion_resistance = 0.0;         // Cohesion resistance threshold.
    double cached_friction_coefficient = 1.0; // Friction coefficient used this frame.
    double strongest_friction_contact_coefficient = 0.0;
    double strongest_friction_contact_force_magnitude = 0.0;
    double strongest_friction_contact_normal_force = 0.0;
    double strongest_friction_contact_tangential_speed = 0.0;
    int strongest_friction_contact_neighbor_x = -1;
    int strongest_friction_contact_neighbor_y = -1;

    // Per-frame gravity and move diagnostics.
    bool carries_transmitted_granular_load = false;
    bool gravity_skipped_for_support = false;
    bool has_granular_support_path = false;
    uint8_t generated_move_direction_mask = DirectionNone;
    uint8_t gravity_compression_candidate_direction_mask = DirectionNone;
    uint8_t incoming_compression_branch_mask = CompressionBranchNone;
    uint8_t jammed_contact_candidate_direction_mask = DirectionNone;
    uint8_t outgoing_compression_branch_mask = CompressionBranchNone;
    uint8_t received_move_direction_mask = DirectionNone;
    uint16_t generated_move_count = 0;
    uint16_t gravity_compression_candidate_count = 0;
    uint16_t incoming_compression_contact_count = 0;
    uint16_t jammed_contact_candidate_count = 0;
    uint16_t outgoing_compression_contact_count = 0;
    uint16_t received_move_count = 0;
    uint16_t downward_generated_move_count = 0;
    uint16_t downward_zero_amount_move_count = 0;
    uint16_t downward_transfer_only_count = 0;
    uint16_t downward_fluid_blocked_contact_count = 0;
    uint16_t downward_elastic_collision_count = 0;
    uint16_t downward_inelastic_collision_count = 0;
    uint16_t downward_absorption_count = 0;
    uint16_t downward_air_target_count = 0;
    uint16_t downward_same_material_target_count = 0;
    uint16_t downward_wall_target_count = 0;
    uint16_t hydrostatic_pressure_injection_count = 0;
    uint16_t dynamic_pressure_target_injection_count = 0;
    uint16_t dynamic_pressure_reflection_injection_count = 0;
    uint16_t excess_move_pressure_injection_count = 0;
    uint16_t successful_outgoing_transfer_count = 0;
    uint16_t successful_incoming_transfer_count = 0;
    uint16_t blocked_outgoing_transfer_count = 0;
    float hydrostatic_pressure_injection_amount = 0.0f;
    float dynamic_pressure_target_injection_amount = 0.0f;
    float dynamic_pressure_reflection_injection_amount = 0.0f;
    float excess_move_pressure_injection_amount = 0.0f;
    float successful_outgoing_transfer_amount = 0.0f;
    float successful_incoming_transfer_amount = 0.0f;
    float blocked_outgoing_transfer_amount = 0.0f;
    double max_incoming_compression_normal_after = 0.0;
    double max_incoming_compression_normal_before = 0.0;
    double max_outgoing_compression_normal_after = 0.0;
    double max_outgoing_compression_normal_before = 0.0;
};

/**
 * ADL functions for automatic JSON serialization.
 */
inline void to_json(nlohmann::json& j, const CellDebug& debug)
{
    j = ReflectSerializer::to_json(debug);
}

inline void from_json(const nlohmann::json& j, CellDebug& debug)
{
    debug = ReflectSerializer::from_json<CellDebug>(j);
}

} // namespace DirtSim
