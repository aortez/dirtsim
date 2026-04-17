#include "core/scenarios/nes/NesSuperMarioBrosSpecialSenses.h"

#include <algorithm>
#include <cstdint>

namespace DirtSim {

namespace {

double normalizeSmb(double value, double maxValue)
{
    return std::clamp(value / maxValue, 0.0, 1.0);
}

double normalizeSmbSigned(double value, double maxMagnitude)
{
    return std::clamp(value / maxMagnitude, -1.0, 1.0);
}

double normalizePhase(uint16_t value, uint16_t period)
{
    return static_cast<double>(value % period) / static_cast<double>(period - 1u);
}

} // namespace

SmbSpecialSenses makeNesSuperMarioBrosSpecialSenses(const NesSuperMarioBrosState& state)
{
    SmbSpecialSenses senses{};
    senses.fill(0.0);

    const double progress =
        (static_cast<double>(state.world) * 4.0 + static_cast<double>(state.level)) / 32.0;
    senses[SmbSpecialSenseIndex::StageProgress] = std::clamp(progress, 0.0, 1.0);
    senses[SmbSpecialSenseIndex::AbsoluteX] =
        normalizeSmb(static_cast<double>(state.absoluteX), 4096.0);
    senses[SmbSpecialSenseIndex::HorizontalSpeed] = state.horizontalSpeedNormalized;
    senses[SmbSpecialSenseIndex::VerticalSpeed] = state.verticalSpeedNormalized;

    if (state.powerupState == SmbPowerupState::Fire) {
        senses[SmbSpecialSenseIndex::Powerup] = 1.0;
    }
    else if (state.powerupState == SmbPowerupState::Big) {
        senses[SmbSpecialSenseIndex::Powerup] = 0.5;
    }

    senses[SmbSpecialSenseIndex::Airborne] = state.airborne ? 1.0 : 0.0;
    senses[SmbSpecialSenseIndex::PlayerYScreen] =
        normalizeSmb(static_cast<double>(state.playerYScreen), 240.0);
    senses[SmbSpecialSenseIndex::Lives] = normalizeSmb(static_cast<double>(state.lives), 9.0);
    senses[SmbSpecialSenseIndex::PlayerXScreen] =
        normalizeSmb(static_cast<double>(state.playerXScreen), 255.0);
    senses[SmbSpecialSenseIndex::NearestEnemyDx] =
        normalizeSmbSigned(static_cast<double>(state.nearestEnemyDx), 255.0);
    senses[SmbSpecialSenseIndex::NearestEnemyDy] =
        normalizeSmbSigned(static_cast<double>(state.nearestEnemyDy), 240.0);
    senses[SmbSpecialSenseIndex::SecondNearestEnemyDx] =
        normalizeSmbSigned(static_cast<double>(state.secondNearestEnemyDx), 255.0);
    senses[SmbSpecialSenseIndex::SecondNearestEnemyDy] =
        normalizeSmbSigned(static_cast<double>(state.secondNearestEnemyDy), 240.0);
    senses[SmbSpecialSenseIndex::EnemyPresent] = state.enemyPresent ? 1.0 : 0.0;
    senses[SmbSpecialSenseIndex::SecondEnemyPresent] = state.secondEnemyPresent ? 1.0 : 0.0;
    senses[SmbSpecialSenseIndex::World] = normalizeSmb(static_cast<double>(state.world), 7.0);
    senses[SmbSpecialSenseIndex::Level] = normalizeSmb(static_cast<double>(state.level), 3.0);
    senses[SmbSpecialSenseIndex::MovementX] = static_cast<double>(state.movementX);
    senses[SmbSpecialSenseIndex::AbsoluteXTilePhase8] = normalizePhase(state.absoluteX, 8u);
    senses[SmbSpecialSenseIndex::PlayerYTilePhase8] =
        normalizePhase(static_cast<uint16_t>(state.playerYScreen), 8u);
    senses[SmbSpecialSenseIndex::AbsoluteXTilePhase16] = normalizePhase(state.absoluteX, 16u);
    senses[SmbSpecialSenseIndex::PlayerYTilePhase16] =
        normalizePhase(static_cast<uint16_t>(state.playerYScreen), 16u);

    return senses;
}

} // namespace DirtSim
