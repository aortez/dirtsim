#pragma once

#include "core/organisms/DuckSensoryData.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"

#include <array>
#include <cstddef>

namespace DirtSim {

namespace SmbSpecialSenseIndex {
constexpr size_t StageProgress = 0u;
constexpr size_t AbsoluteX = 1u;
constexpr size_t HorizontalSpeed = 2u;
constexpr size_t VerticalSpeed = 3u;
constexpr size_t Powerup = 4u;
constexpr size_t Airborne = 5u;
constexpr size_t PlayerYScreen = 6u;
constexpr size_t Lives = 7u;
constexpr size_t PlayerXScreen = 8u;
constexpr size_t NearestEnemyDx = 9u;
constexpr size_t NearestEnemyDy = 10u;
constexpr size_t SecondNearestEnemyDx = 11u;
constexpr size_t SecondNearestEnemyDy = 12u;
constexpr size_t EnemyPresent = 13u;
constexpr size_t SecondEnemyPresent = 14u;
constexpr size_t World = 15u;
constexpr size_t Level = 16u;
constexpr size_t MovementX = 17u;
constexpr size_t AbsoluteXTilePhase8 = 18u;
constexpr size_t PlayerYTilePhase8 = 19u;
constexpr size_t AbsoluteXTilePhase16 = 20u;
constexpr size_t PlayerYTilePhase16 = 21u;
} // namespace SmbSpecialSenseIndex

using SmbSpecialSenses = std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT>;

SmbSpecialSenses makeNesSuperMarioBrosSpecialSenses(const NesSuperMarioBrosState& state);

} // namespace DirtSim
