#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"

#include <array>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

constexpr size_t kEnemySlotCount = 5;
constexpr size_t kFacingDirectionAddr = 0x0033;
constexpr size_t kMovementDirectionAddr = 0x0045;
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

SmolnesRuntime::MemorySnapshot makeSmbSnapshot(
    uint8_t world,
    uint8_t level,
    uint8_t playerXPage,
    uint8_t playerXScreen,
    uint8_t horizontalSpeed,
    uint8_t verticalSpeed,
    uint8_t playerYScreen,
    uint8_t powerupState,
    uint8_t playerState,
    uint8_t playerFloatState,
    uint8_t lives,
    uint8_t gameEngine)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    snapshot.cpuRam[0x0770] = gameEngine;
    snapshot.cpuRam[0x000E] = playerState;
    snapshot.cpuRam[0x001D] = playerFloatState;
    snapshot.cpuRam[0x0086] = playerXScreen;
    snapshot.cpuRam[0x006D] = playerXPage;
    snapshot.cpuRam[0x075A] = lives;
    snapshot.cpuRam[0x075F] = world;
    snapshot.cpuRam[0x0760] = level;
    snapshot.cpuRam[0x0057] = horizontalSpeed;
    snapshot.cpuRam[0x009F] = verticalSpeed;
    snapshot.cpuRam[0x00CE] = playerYScreen;
    snapshot.cpuRam[0x0756] = powerupState;

    return snapshot;
}

void setEnemySlot(
    SmolnesRuntime::MemorySnapshot& snapshot,
    size_t slot,
    uint8_t active,
    uint8_t type,
    uint8_t xPage,
    uint8_t xScreen,
    uint8_t yScreen)
{
    ASSERT_LT(slot, kEnemySlotCount);
    snapshot.cpuRam[kEnemyActiveAddrs[slot]] = active;
    snapshot.cpuRam[kEnemyTypeAddrs[slot]] = type;
    snapshot.cpuRam[kEnemyXPageAddrs[slot]] = xPage;
    snapshot.cpuRam[kEnemyXScreenAddrs[slot]] = xScreen;
    snapshot.cpuRam[kEnemyYScreenAddrs[slot]] = yScreen;
}

} // namespace

TEST(NesSuperMarioBrosRamExtractorTest, ExtractDecodesGameplayState)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        1,
        2,
        0x03,
        0x80,
        25,
        static_cast<uint8_t>(static_cast<int8_t>(-40)),
        120,
        2,
        0x08,
        0x00,
        3,
        1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Alive);
    EXPECT_EQ(state.playerState, SmbPlayerState::Normal);
    EXPECT_EQ(state.floatState, SmbFloatState::GroundedOrOther);
    EXPECT_EQ(state.powerupState, SmbPowerupState::Fire);
    EXPECT_FALSE(state.airborne);
    EXPECT_NEAR(state.horizontalSpeedNormalized, 25.0 / 40.0, 1e-6);
    EXPECT_NEAR(state.verticalSpeedNormalized, -40.0 / 128.0, 1e-6);
    EXPECT_EQ(state.world, 1u);
    EXPECT_EQ(state.level, 2u);
    EXPECT_EQ(state.absoluteX, 0x0380u);
    EXPECT_EQ(state.playerXScreen, 0x80u);
    EXPECT_EQ(state.playerYScreen, 120u);
    EXPECT_EQ(state.lives, 3u);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractUsesFloatStateForAirborne)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        0,
        0,
        0x00,
        0x20,
        static_cast<uint8_t>(static_cast<int8_t>(-5)),
        0,
        100,
        1,
        0x08,
        0x01,
        2,
        1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Alive);
    EXPECT_EQ(state.playerState, SmbPlayerState::Normal);
    EXPECT_EQ(state.floatState, SmbFloatState::Jumping);
    EXPECT_EQ(state.powerupState, SmbPowerupState::Big);
    EXPECT_TRUE(state.airborne);
    EXPECT_NEAR(state.horizontalSpeedNormalized, -5.0 / 40.0, 1e-6);
    EXPECT_DOUBLE_EQ(state.verticalSpeedNormalized, 0.0);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractDecodesFacingAndMovementDirections)
{
    SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x00, 0x20, 0, 0, 100, 0, 0x08, 0x00, 3, 1);
    snapshot.cpuRam[kFacingDirectionAddr] = 1u;
    snapshot.cpuRam[kMovementDirectionAddr] = 2u;

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_FLOAT_EQ(state.facingX, 1.0f);
    EXPECT_FLOAT_EQ(state.movementX, -1.0f);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractMapsDeathAnimationState)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        0,
        0,
        0x00,
        0x20,
        static_cast<uint8_t>(static_cast<int8_t>(-5)),
        0,
        100,
        1,
        0x0B,
        0x00,
        0,
        1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Dying);
    EXPECT_EQ(state.playerState, SmbPlayerState::Dying);
    EXPECT_EQ(state.floatState, SmbFloatState::GroundedOrOther);
    EXPECT_EQ(state.powerupState, SmbPowerupState::Big);
    EXPECT_FALSE(state.airborne);
    EXPECT_NEAR(state.horizontalSpeedNormalized, -5.0 / 40.0, 1e-6);
    EXPECT_DOUBLE_EQ(state.verticalSpeedNormalized, 0.0);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractMapsPlayerDiesState)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        0,
        0,
        0x00,
        0x20,
        static_cast<uint8_t>(static_cast<int8_t>(-5)),
        0,
        100,
        1,
        0x06,
        0x00,
        2,
        1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Dying);
    EXPECT_EQ(state.playerState, SmbPlayerState::PlayerDies);
    EXPECT_EQ(state.floatState, SmbFloatState::GroundedOrOther);
    EXPECT_FALSE(state.airborne);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractMapsReloadScreenStateAsDead)
{
    const SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x00, 0x00, 0, 0, 0, 0, 0x00, 0x00, 1, 1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Dead);
    EXPECT_EQ(state.playerState, SmbPlayerState::LeftmostOfScreen);
    EXPECT_EQ(state.floatState, SmbFloatState::GroundedOrOther);
    EXPECT_EQ(state.powerupState, SmbPowerupState::Small);
    EXPECT_FALSE(state.airborne);
    EXPECT_DOUBLE_EQ(state.horizontalSpeedNormalized, 0.0);
    EXPECT_DOUBLE_EQ(state.verticalSpeedNormalized, 0.0);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractTreatsNonDeathPlayerModesAsAlive)
{
    const SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x00, 0x20, 0, 0, 100, 0, 0x09, 0x00, 3, 1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Alive);
    EXPECT_EQ(state.playerState, SmbPlayerState::Growing);
    EXPECT_EQ(state.floatState, SmbFloatState::GroundedOrOther);
    EXPECT_FALSE(state.airborne);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractPreservesKnownFloatStateDistinctions)
{
    NesSuperMarioBrosRamExtractor extractor;

    const NesSuperMarioBrosState walkedOffLedge =
        extractor.extract(makeSmbSnapshot(0, 0, 0x00, 0x20, 0, 0, 100, 0, 0x08, 0x02, 3, 1), true);
    EXPECT_EQ(walkedOffLedge.floatState, SmbFloatState::WalkedOffLedge);
    EXPECT_TRUE(walkedOffLedge.airborne);

    const NesSuperMarioBrosState slidingFlagpole =
        extractor.extract(makeSmbSnapshot(0, 0, 0x00, 0x20, 0, 0, 100, 0, 0x08, 0x03, 3, 1), true);
    EXPECT_EQ(slidingFlagpole.floatState, SmbFloatState::SlidingFlagpole);
    EXPECT_FALSE(slidingFlagpole.airborne);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractMapsUnknownRawPlayerModeFields)
{
    const SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x00, 0x20, 0, 0, 100, 0, 0xFE, 0xFE, 3, 1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.playerState, SmbPlayerState::Unknown);
    EXPECT_EQ(state.floatState, SmbFloatState::Unknown);
    EXPECT_EQ(state.lifeState, SmbLifeState::Alive);
    EXPECT_FALSE(state.airborne);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractTracksNearestAndSecondNearestEnemies)
{
    SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(1, 0, 0x03, 0x80, 0, 0, 120, 0, 0x08, 0x00, 3, 1);
    setEnemySlot(snapshot, 0, 1, 6, 0x03, 0xF0, 118);
    setEnemySlot(snapshot, 1, 1, 6, 0x03, 0x90, 110);
    setEnemySlot(snapshot, 2, 1, 6, 0x03, 0x50, 100);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_TRUE(state.enemyPresent);
    EXPECT_EQ(state.nearestEnemyDx, 16);
    EXPECT_EQ(state.nearestEnemyDy, -10);
    EXPECT_EQ(state.secondNearestEnemyDx, -48);
    EXPECT_EQ(state.secondNearestEnemyDy, -20);
}
