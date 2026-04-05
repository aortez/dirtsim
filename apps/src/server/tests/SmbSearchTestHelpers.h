#pragma once

#include "core/input/PlayerControlFrame.h"
#include "core/scenarios/tests/NesTestRomPath.h"

#include <gtest/gtest.h>

namespace DirtSim::Test {

inline void requireSmbRomOrSkip()
{
    if (!resolveSmbRomPath().has_value()) {
        GTEST_SKIP() << "DIRTSIM_NES_SMB_TEST_ROM_PATH or testdata/roms/smb.nes is required.";
    }
}

inline void expectFrameEq(const PlayerControlFrame& actual, const PlayerControlFrame& expected)
{
    EXPECT_EQ(actual.xAxis, expected.xAxis);
    EXPECT_EQ(actual.yAxis, expected.yAxis);
    EXPECT_EQ(actual.buttons, expected.buttons);
}

} // namespace DirtSim::Test
