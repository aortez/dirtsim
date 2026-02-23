#pragma once

#include "core/organisms/DuckSensoryData.h"

namespace DirtSim::NesDuckSpecialSenseLayout {

enum Slot : int {
    Bias = 0,
    BirdYNormalized = 1,
    BirdVelocityNormalized = 2,
    NextPipeDistanceNormalized = 3,
    NextPipeTopNormalized = 4,
    NextPipeBottomNormalized = 5,
    BirdGapOffsetNormalized = 6,
    ScrollXNormalized = 7,
    ScrollNt = 8,
    GameStateNormalized = 9,
    ScoreNormalized = 10,
    PrevFlapPressed = 11,
};

inline constexpr int SlotCount = DuckSensoryData::SPECIAL_SENSE_COUNT;
inline constexpr int FlappyMappedCount = 12;

static_assert(
    PrevFlapPressed < SlotCount,
    "NesDuckSpecialSenseLayout: Flappy mapping must fit within Duck special senses");

} // namespace DirtSim::NesDuckSpecialSenseLayout
