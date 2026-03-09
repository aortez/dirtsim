#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

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
    uint8_t lives,
    uint8_t gameEngine)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    snapshot.cpuRam[0x0770] = gameEngine;
    snapshot.cpuRam[0x000E] = playerState;
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

} // namespace

TEST(NesSuperMarioBrosRamExtractorTest, ExtractDecodesGameplayState)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        1, 2, 0x03, 0x80, 25, static_cast<uint8_t>(static_cast<int8_t>(-40)), 120, 2, 0x08, 3, 1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Alive);
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

TEST(NesSuperMarioBrosRamExtractorTest, ExtractTreatsAirborneGameplayStatesAsAlive)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        0, 0, 0x00, 0x20, static_cast<uint8_t>(static_cast<int8_t>(-5)), 0, 100, 1, 0x02, 2, 1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Alive);
    EXPECT_EQ(state.powerupState, SmbPowerupState::Big);
    EXPECT_TRUE(state.airborne);
    EXPECT_NEAR(state.horizontalSpeedNormalized, -5.0 / 40.0, 1e-6);
    EXPECT_DOUBLE_EQ(state.verticalSpeedNormalized, 0.0);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractMapsDeathAnimationState)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        0, 0, 0x00, 0x20, static_cast<uint8_t>(static_cast<int8_t>(-5)), 0, 100, 1, 0x0B, 0, 1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Dying);
    EXPECT_EQ(state.powerupState, SmbPowerupState::Big);
    EXPECT_FALSE(state.airborne);
    EXPECT_NEAR(state.horizontalSpeedNormalized, -5.0 / 40.0, 1e-6);
    EXPECT_DOUBLE_EQ(state.verticalSpeedNormalized, 0.0);
}

TEST(NesSuperMarioBrosRamExtractorTest, ExtractMapsReloadScreenStateAsDead)
{
    const SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x00, 0x00, 0, 0, 0, 0, 0x00, 1, 1);

    NesSuperMarioBrosRamExtractor extractor;
    const NesSuperMarioBrosState state = extractor.extract(snapshot, true);

    EXPECT_EQ(state.phase, SmbPhase::Gameplay);
    EXPECT_EQ(state.lifeState, SmbLifeState::Dead);
    EXPECT_EQ(state.powerupState, SmbPowerupState::Small);
    EXPECT_FALSE(state.airborne);
    EXPECT_DOUBLE_EQ(state.horizontalSpeedNormalized, 0.0);
    EXPECT_DOUBLE_EQ(state.verticalSpeedNormalized, 0.0);
}
