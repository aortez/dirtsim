#pragma once

#include "ClockEventTypes.h"

namespace DirtSim {

class World;

namespace ClockEvents {

/**
 * Initializes the Meltdown event by converting digit cells to falling material.
 *
 * @param state The state to initialize.
 * @param world The world to modify.
 * @return The digit material type used (for tracking during meltdown).
 */
void startMeltdown(MeltdownEventState& state, World& world);

/**
 * Updates the Meltdown event, converting fallen digits to water.
 *
 * @param state The event state.
 * @param world The world to modify.
 * @param remaining_time Reference to remaining time (may be set to 0 to end early).
 * @param event_duration Total event duration (for calculating elapsed time).
 * @param drain_open Whether the drain is currently open.
 * @param drain_start_x Start X of drain opening.
 * @param drain_end_x End X of drain opening.
 */
void updateMeltdown(
    MeltdownEventState& state,
    World& world,
    double& remaining_time,
    double event_duration,
    bool drain_open,
    int16_t drain_start_x,
    int16_t drain_end_x);

/**
 * Converts any remaining digit material to water after meltdown ends.
 *
 * @param world The world to clean up.
 * @param digit_material The material type to convert.
 */
void endMeltdown(World& world, Material::EnumType digit_material);

} // namespace ClockEvents
} // namespace DirtSim
