#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"

#include <algorithm>
#include <cstdint>

namespace DirtSim {

namespace {

constexpr size_t kFacingDirectionAddr = 0x0700;
constexpr size_t kGameEngineSubroutineAddr = 0x0770;
constexpr size_t kHorizontalSpeedAddr = 0x0057;
constexpr size_t kLevelAddr = 0x0760;
constexpr size_t kLivesAddr = 0x075A;
constexpr size_t kPlayerStateAddr = 0x000E;
constexpr size_t kPlayerXPageAddr = 0x006D;
constexpr size_t kPlayerXScreenAddr = 0x0086;
constexpr size_t kPlayerYScreenAddr = 0x00CE;
constexpr size_t kPowerupStateAddr = 0x0756;
constexpr size_t kVerticalSpeedAddr = 0x009F;
constexpr size_t kWorldAddr = 0x075F;

constexpr uint8_t kFacingLeft = 2;
constexpr uint8_t kGameEngineGameplay = 1;
constexpr uint8_t kPlayerStateActiveGameplay = 0x08;
constexpr uint8_t kPlayerStateDeathAnimation = 0x0B;
constexpr uint8_t kPlayerStateReloadScreen = 0x00;
constexpr uint8_t kPlayerStateRespawnTransition = 0x06;

uint16_t decodeAbsoluteX(uint8_t playerXPage, uint8_t playerXScreen)
{
    return (static_cast<uint16_t>(playerXPage) << 8) | static_cast<uint16_t>(playerXScreen);
}

double decodeHorizontalSpeedNormalized(uint8_t horizontalSpeed, uint8_t facingDirection)
{
    const double sign = facingDirection == kFacingLeft ? -1.0 : 1.0;
    return std::clamp(sign * static_cast<double>(horizontalSpeed) / 40.0, -1.0, 1.0);
}

SmbLifeState decodeLifeState(uint8_t playerState)
{
    // Probe runs show 0x08 during active gameplay and 0x0B during the death animation.
    switch (playerState) {
        case 0x01:
        case 0x02:
        case 0x03:
        case kPlayerStateActiveGameplay:
            return SmbLifeState::Alive;
        case kPlayerStateDeathAnimation:
            return SmbLifeState::Dying;
        case kPlayerStateReloadScreen:
        case kPlayerStateRespawnTransition:
            return SmbLifeState::Dead;
        default:
            return SmbLifeState::Dying;
    }
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

double decodeVerticalSpeedNormalized(uint8_t verticalSpeed)
{
    return std::clamp(static_cast<double>(static_cast<int8_t>(verticalSpeed)) / 128.0, -1.0, 1.0);
}

bool isAirborne(uint8_t playerState)
{
    return playerState >= 1u && playerState <= 3u;
}

} // namespace

NesSuperMarioBrosState NesSuperMarioBrosRamExtractor::extract(
    const SmolnesRuntime::MemorySnapshot& snapshot, bool setupComplete) const
{
    const uint8_t playerState = snapshot.cpuRam.at(kPlayerStateAddr);

    NesSuperMarioBrosState output;
    output.phase = decodePhase(snapshot.cpuRam.at(kGameEngineSubroutineAddr), setupComplete);
    output.lifeState = decodeLifeState(playerState);
    output.powerupState = decodePowerupState(snapshot.cpuRam.at(kPowerupStateAddr));
    output.airborne = isAirborne(playerState);
    output.horizontalSpeedNormalized = decodeHorizontalSpeedNormalized(
        snapshot.cpuRam.at(kHorizontalSpeedAddr), snapshot.cpuRam.at(kFacingDirectionAddr));
    output.verticalSpeedNormalized =
        decodeVerticalSpeedNormalized(snapshot.cpuRam.at(kVerticalSpeedAddr));
    output.world = snapshot.cpuRam.at(kWorldAddr);
    output.level = snapshot.cpuRam.at(kLevelAddr);
    output.absoluteX = decodeAbsoluteX(
        snapshot.cpuRam.at(kPlayerXPageAddr), snapshot.cpuRam.at(kPlayerXScreenAddr));
    output.playerXScreen = snapshot.cpuRam.at(kPlayerXScreenAddr);
    output.playerYScreen = snapshot.cpuRam.at(kPlayerYScreenAddr);
    output.lives = snapshot.cpuRam.at(kLivesAddr);
    return output;
}

} // namespace DirtSim
