#pragma once

#include "core/input/PlayerControlFrame.h"
#include "core/scenarios/tests/NesTestRomPath.h"

#include <gtest/gtest.h>

namespace DirtSim::Test {

inline bool hasSmbRom()
{
    return resolveSmbRomPath().has_value();
}

inline void expectFrameEq(const PlayerControlFrame& actual, const PlayerControlFrame& expected)
{
    EXPECT_EQ(actual.xAxis, expected.xAxis);
    EXPECT_EQ(actual.yAxis, expected.yAxis);
    EXPECT_EQ(actual.buttons, expected.buttons);
}

} // namespace DirtSim::Test

#define REQUIRE_SMB_ROM_OR_SKIP()                                                        \
    do {                                                                                 \
        if (!::DirtSim::Test::hasSmbRom()) {                                             \
            GTEST_SKIP() << "DIRTSIM_NES_SMB_TEST_ROM_PATH or testdata/roms/smb.nes is " \
                            "required.";                                                 \
        }                                                                                \
    } while (false)
