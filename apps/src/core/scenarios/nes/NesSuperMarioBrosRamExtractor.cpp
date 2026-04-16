#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace DirtSim {

namespace {

constexpr size_t kGameEngineSubroutineAddr = 0x0770;
constexpr size_t kHorizontalSpeedAddr = 0x0057;
constexpr size_t kLevelAddr = 0x0760;
constexpr size_t kLivesAddr = 0x075A;
constexpr size_t kEnemySlotCount = 5;
constexpr size_t kFacingDirectionAddr = 0x0033;
constexpr size_t kMovementDirectionAddr = 0x0045;
constexpr size_t kPlayerFloatStateAddr = 0x001D;
constexpr size_t kPlayerStateAddr = 0x000E;
constexpr size_t kPlayerXPageAddr = 0x006D;
constexpr size_t kPlayerXScreenAddr = 0x0086;
constexpr size_t kPlayerYScreenAddr = 0x00CE;
constexpr size_t kPowerupStateAddr = 0x0756;
constexpr size_t kVerticalSpeedAddr = 0x009F;
constexpr size_t kWorldAddr = 0x075F;
constexpr std::array<size_t, kEnemySlotCount> kEnemyActiveAddrs = {
    0x000F, 0x0010, 0x0011, 0x0012, 0x0013
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyTypeAddrs = {
    0x0016, 0x0017, 0x0018, 0x0019, 0x001A
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyXPageAddrs = {
    0x006E, 0x006F, 0x0070, 0x0071, 0x0072
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyXScreenAddrs = {
    0x0087, 0x0088, 0x0089, 0x008A, 0x008B
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyYScreenAddrs = {
    0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3
};

constexpr uint8_t kGameEngineGameplay = 1;

uint16_t decodeAbsoluteX(uint8_t playerXPage, uint8_t playerXScreen)
{
    return (static_cast<uint16_t>(playerXPage) << 8) | static_cast<uint16_t>(playerXScreen);
}

double decodeHorizontalSpeedNormalized(uint8_t horizontalSpeed)
{
    return std::clamp(static_cast<double>(static_cast<int8_t>(horizontalSpeed)) / 40.0, -1.0, 1.0);
}

float decodeFacingX(uint8_t rawFacingDirection)
{
    switch (rawFacingDirection) {
        case 1u:
            return 1.0f;
        case 2u:
            return -1.0f;
        default:
            return 0.0f;
    }
}

SmbFloatState decodeFloatState(uint8_t playerFloatState)
{
    switch (playerFloatState) {
        case 0x00u:
            return SmbFloatState::GroundedOrOther;
        case 0x01u:
            return SmbFloatState::Jumping;
        case 0x02u:
            return SmbFloatState::WalkedOffLedge;
        case 0x03u:
            return SmbFloatState::SlidingFlagpole;
        default:
            return SmbFloatState::Unknown;
    }
}

SmbLifeState decodeLifeState(SmbPlayerState playerState)
{
    // 0x000E is a broad player-mode register. For the coarse life-state model we only treat the
    // explicit death lifecycle states as non-alive.
    switch (playerState) {
        case SmbPlayerState::LeftmostOfScreen:
            return SmbLifeState::Dead;
        case SmbPlayerState::ClimbingVine:
        case SmbPlayerState::EnteringReversedLPipe:
        case SmbPlayerState::GoingDownPipe:
        case SmbPlayerState::Autowalk:
        case SmbPlayerState::AutowalkAlt:
        case SmbPlayerState::EnteringArea:
        case SmbPlayerState::Normal:
        case SmbPlayerState::Growing:
        case SmbPlayerState::Shrinking:
        case SmbPlayerState::BecomingFire:
        case SmbPlayerState::Unknown:
            return SmbLifeState::Alive;
        case SmbPlayerState::PlayerDies:
        case SmbPlayerState::Dying:
            return SmbLifeState::Dying;
    }

    return SmbLifeState::Alive;
}

SmbPhase decodePhase(uint8_t gameEngine, bool setupComplete)
{
    return setupComplete && gameEngine == kGameEngineGameplay ? SmbPhase::Gameplay
                                                              : SmbPhase::NonGameplay;
}

SmbPowerupState decodePowerupState(uint8_t powerupState)
{
    if (powerupState >= 2u) {
        return SmbPowerupState::Fire;
    }
    if (powerupState == 1u) {
        return SmbPowerupState::Big;
    }
    return SmbPowerupState::Small;
}

SmbPlayerState decodePlayerState(uint8_t playerState)
{
    switch (playerState) {
        case 0x00u:
            return SmbPlayerState::LeftmostOfScreen;
        case 0x01u:
            return SmbPlayerState::ClimbingVine;
        case 0x02u:
            return SmbPlayerState::EnteringReversedLPipe;
        case 0x03u:
            return SmbPlayerState::GoingDownPipe;
        case 0x04u:
            return SmbPlayerState::Autowalk;
        case 0x05u:
            return SmbPlayerState::AutowalkAlt;
        case 0x06u:
            return SmbPlayerState::PlayerDies;
        case 0x07u:
            return SmbPlayerState::EnteringArea;
        case 0x08u:
            return SmbPlayerState::Normal;
        case 0x09u:
            return SmbPlayerState::Growing;
        case 0x0Au:
            return SmbPlayerState::Shrinking;
        case 0x0Bu:
            return SmbPlayerState::Dying;
        case 0x0Cu:
            return SmbPlayerState::BecomingFire;
        default:
            return SmbPlayerState::Unknown;
    }
}

double decodeVerticalSpeedNormalized(uint8_t verticalSpeed)
{
    return std::clamp(static_cast<double>(static_cast<int8_t>(verticalSpeed)) / 128.0, -1.0, 1.0);
}

struct DecodedEnemy {
    int16_t dx = 0;
    int16_t dy = 0;
    uint32_t distanceSquared = 0;
};

DecodedEnemy decodeEnemy(
    uint8_t enemyXPage,
    uint8_t enemyXScreen,
    uint8_t enemyYScreen,
    uint16_t playerAbsoluteX,
    uint8_t playerYScreen)
{
    const uint16_t enemyAbsoluteX = decodeAbsoluteX(enemyXPage, enemyXScreen);
    const int16_t dx = static_cast<int16_t>(enemyAbsoluteX) - static_cast<int16_t>(playerAbsoluteX);
    const int16_t dy = static_cast<int16_t>(enemyYScreen) - static_cast<int16_t>(playerYScreen);

    const int32_t dx32 = static_cast<int32_t>(dx);
    const int32_t dy32 = static_cast<int32_t>(dy);

    return {
        .dx = dx,
        .dy = dy,
        .distanceSquared = static_cast<uint32_t>((dx32 * dx32) + (dy32 * dy32)),
    };
}

bool isAirborne(SmbFloatState playerFloatState)
{
    return playerFloatState == SmbFloatState::Jumping
        || playerFloatState == SmbFloatState::WalkedOffLedge;
}

} // namespace

NesSuperMarioBrosState NesSuperMarioBrosRamExtractor::extract(
    const SmolnesRuntime::MemorySnapshot& snapshot, bool setupComplete) const
{
    const SmbPlayerState playerState = decodePlayerState(snapshot.cpuRam.at(kPlayerStateAddr));
    const SmbFloatState playerFloatState =
        decodeFloatState(snapshot.cpuRam.at(kPlayerFloatStateAddr));
    const uint16_t playerAbsoluteX = decodeAbsoluteX(
        snapshot.cpuRam.at(kPlayerXPageAddr), snapshot.cpuRam.at(kPlayerXScreenAddr));
    const uint8_t playerYScreen = snapshot.cpuRam.at(kPlayerYScreenAddr);

    NesSuperMarioBrosState output;
    output.phase = decodePhase(snapshot.cpuRam.at(kGameEngineSubroutineAddr), setupComplete);
    output.lifeState = decodeLifeState(playerState);
    output.playerState = playerState;
    output.floatState = playerFloatState;
    output.powerupState = decodePowerupState(snapshot.cpuRam.at(kPowerupStateAddr));
    output.airborne = isAirborne(playerFloatState);
    output.facingX = decodeFacingX(snapshot.cpuRam.at(kFacingDirectionAddr));
    output.movementX = decodeFacingX(snapshot.cpuRam.at(kMovementDirectionAddr));
    output.horizontalSpeedNormalized =
        decodeHorizontalSpeedNormalized(snapshot.cpuRam.at(kHorizontalSpeedAddr));
    output.verticalSpeedNormalized =
        decodeVerticalSpeedNormalized(snapshot.cpuRam.at(kVerticalSpeedAddr));
    output.world = snapshot.cpuRam.at(kWorldAddr);
    output.level = snapshot.cpuRam.at(kLevelAddr);
    output.absoluteX = playerAbsoluteX;
    output.playerXScreen = snapshot.cpuRam.at(kPlayerXScreenAddr);
    output.playerYScreen = playerYScreen;
    output.lives = snapshot.cpuRam.at(kLivesAddr);

    std::array<DecodedEnemy, 2> nearestEnemies{};
    size_t nearestEnemyCount = 0;
    for (size_t slot = 0; slot < kEnemySlotCount; ++slot) {
        if (snapshot.cpuRam.at(kEnemyActiveAddrs[slot]) == 0u) {
            continue;
        }
        if (snapshot.cpuRam.at(kEnemyTypeAddrs[slot]) == 0u) {
            continue;
        }

        const DecodedEnemy decodedEnemy = decodeEnemy(
            snapshot.cpuRam.at(kEnemyXPageAddrs[slot]),
            snapshot.cpuRam.at(kEnemyXScreenAddrs[slot]),
            snapshot.cpuRam.at(kEnemyYScreenAddrs[slot]),
            playerAbsoluteX,
            playerYScreen);

        if (nearestEnemyCount == 0u) {
            nearestEnemies[0] = decodedEnemy;
            nearestEnemyCount = 1u;
            continue;
        }

        if (nearestEnemyCount == 1u) {
            nearestEnemies[1] = decodedEnemy;
            nearestEnemyCount = 2u;
            if (nearestEnemies[1].distanceSquared < nearestEnemies[0].distanceSquared) {
                std::swap(nearestEnemies[0], nearestEnemies[1]);
            }
            continue;
        }

        if (decodedEnemy.distanceSquared >= nearestEnemies.back().distanceSquared) {
            continue;
        }

        nearestEnemies.back() = decodedEnemy;
        if (nearestEnemies[1].distanceSquared < nearestEnemies[0].distanceSquared) {
            std::swap(nearestEnemies[0], nearestEnemies[1]);
        }
    }

    if (nearestEnemyCount > 0u) {
        output.enemyPresent = true;
        output.nearestEnemyDx = nearestEnemies[0].dx;
        output.nearestEnemyDy = nearestEnemies[0].dy;
    }
    if (nearestEnemyCount > 1u) {
        output.secondEnemyPresent = true;
        output.secondNearestEnemyDx = nearestEnemies[1].dx;
        output.secondNearestEnemyDy = nearestEnemies[1].dy;
    }

    return output;
}

} // namespace DirtSim
