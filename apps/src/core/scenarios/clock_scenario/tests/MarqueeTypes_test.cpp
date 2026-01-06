#include "core/scenarios/clock_scenario/MarqueeTypes.h"
#include <gtest/gtest.h>

using namespace DirtSim;

// =============================================================================
// layoutString Tests
// =============================================================================

TEST(MarqueeTypesTest, LayoutString_SingleDigit)
{
    auto placements = layoutString("5", 5, 1, 1);

    ASSERT_EQ(placements.size(), 1u);
    EXPECT_EQ(placements[0].c, '5');
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_DOUBLE_EQ(placements[0].y, 0.0);
}

TEST(MarqueeTypesTest, LayoutString_TwoDigits)
{
    auto placements = layoutString("12", 5, 1, 1);

    ASSERT_EQ(placements.size(), 2u);
    EXPECT_EQ(placements[0].c, '1');
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_EQ(placements[1].c, '2');
    EXPECT_DOUBLE_EQ(placements[1].x, 5.0);  // After first digit.
}

TEST(MarqueeTypesTest, LayoutString_DigitsWithSpace)
{
    auto placements = layoutString("1 2", 5, 2, 1);

    ASSERT_EQ(placements.size(), 2u);
    EXPECT_EQ(placements[0].c, '1');
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_EQ(placements[1].c, '2');
    EXPECT_DOUBLE_EQ(placements[1].x, 7.0);  // 5 (digit) + 2 (gap).
}

TEST(MarqueeTypesTest, LayoutString_WithColon)
{
    auto placements = layoutString("1:2", 5, 1, 3);

    ASSERT_EQ(placements.size(), 3u);
    EXPECT_EQ(placements[0].c, '1');
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_EQ(placements[1].c, ':');
    EXPECT_DOUBLE_EQ(placements[1].x, 5.0);
    EXPECT_EQ(placements[2].c, '2');
    EXPECT_DOUBLE_EQ(placements[2].x, 8.0);  // 5 + 3 (colon).
}

TEST(MarqueeTypesTest, LayoutString_TimeFormat)
{
    // Typical time format: "1 2 : 3 4".
    auto placements = layoutString("1 2 : 3 4", 5, 1, 2);

    ASSERT_EQ(placements.size(), 5u);

    // '1' at x=0.
    EXPECT_EQ(placements[0].c, '1');
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);

    // '2' at x=6 (digit=5 + gap=1).
    EXPECT_EQ(placements[1].c, '2');
    EXPECT_DOUBLE_EQ(placements[1].x, 6.0);

    // ':' at x=12 (6 + digit=5 + gap=1).
    EXPECT_EQ(placements[2].c, ':');
    EXPECT_DOUBLE_EQ(placements[2].x, 12.0);

    // '3' at x=15 (12 + colon=2 + gap=1).
    EXPECT_EQ(placements[3].c, '3');
    EXPECT_DOUBLE_EQ(placements[3].x, 15.0);

    // '4' at x=21 (15 + digit=5 + gap=1).
    EXPECT_EQ(placements[4].c, '4');
    EXPECT_DOUBLE_EQ(placements[4].x, 21.0);
}

TEST(MarqueeTypesTest, LayoutString_EmptyString)
{
    auto placements = layoutString("", 5, 1, 1);

    EXPECT_TRUE(placements.empty());
}

TEST(MarqueeTypesTest, LayoutString_OnlySpaces)
{
    auto placements = layoutString("   ", 5, 2, 1);

    // Spaces don't produce placements, just advance position.
    EXPECT_TRUE(placements.empty());
}

TEST(MarqueeTypesTest, LayoutString_AllDigits)
{
    auto placements = layoutString("0123456789", 5, 1, 1);

    ASSERT_EQ(placements.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(placements[i].c, '0' + i);
        EXPECT_DOUBLE_EQ(placements[i].x, i * 5.0);
    }
}

// =============================================================================
// calculateStringWidth Tests
// =============================================================================

TEST(MarqueeTypesTest, CalculateWidth_SingleDigit)
{
    int width = calculateStringWidth("5", 5, 1, 1);
    EXPECT_EQ(width, 5);
}

TEST(MarqueeTypesTest, CalculateWidth_TwoDigits)
{
    int width = calculateStringWidth("12", 5, 1, 1);
    EXPECT_EQ(width, 10);
}

TEST(MarqueeTypesTest, CalculateWidth_DigitsWithSpace)
{
    int width = calculateStringWidth("1 2", 5, 2, 1);
    EXPECT_EQ(width, 12);  // 5 + 2 + 5.
}

TEST(MarqueeTypesTest, CalculateWidth_WithColon)
{
    int width = calculateStringWidth("1:2", 5, 1, 3);
    EXPECT_EQ(width, 13);  // 5 + 3 + 5.
}

TEST(MarqueeTypesTest, CalculateWidth_TimeFormat)
{
    // "1 2 : 3 4" = d + g + d + g + c + g + d + g + d.
    // = 5 + 1 + 5 + 1 + 2 + 1 + 5 + 1 + 5 = 26.
    int width = calculateStringWidth("1 2 : 3 4", 5, 1, 2);
    EXPECT_EQ(width, 26);
}

TEST(MarqueeTypesTest, CalculateWidth_EmptyString)
{
    int width = calculateStringWidth("", 5, 1, 1);
    EXPECT_EQ(width, 0);
}

TEST(MarqueeTypesTest, CalculateWidth_OnlySpaces)
{
    int width = calculateStringWidth("   ", 5, 2, 1);
    EXPECT_EQ(width, 6);  // 3 gaps of 2 each.
}

// =============================================================================
// HorizontalScroll Tests
// =============================================================================

TEST(MarqueeTypesTest, HorizontalScroll_StartInitializesState)
{
    HorizontalScrollState state;
    startHorizontalScroll(state, "12:34", 100.0, 50.0, 10, 2, 5);

    EXPECT_DOUBLE_EQ(state.viewport_x, 0.0);
    EXPECT_DOUBLE_EQ(state.visible_width, 100.0);
    EXPECT_DOUBLE_EQ(state.speed, 50.0);
    EXPECT_TRUE(state.scrolling_out);
    EXPECT_EQ(state.digit_width, 10);
    EXPECT_EQ(state.digit_gap, 2);
    EXPECT_EQ(state.colon_width, 5);
    // Content width: 10 + 10 + 5 + 10 + 10 = 45.
    EXPECT_DOUBLE_EQ(state.content_width, 45.0);
}

TEST(MarqueeTypesTest, HorizontalScroll_UpdateAdvancesViewport)
{
    HorizontalScrollState state;
    startHorizontalScroll(state, "12", 100.0, 50.0, 10, 2, 5);

    auto frame = updateHorizontalScroll(state, "12", 0.1);

    // After 0.1s at 50 units/s, viewport should advance 5 units.
    EXPECT_DOUBLE_EQ(state.viewport_x, 5.0);
    EXPECT_DOUBLE_EQ(frame.viewportX, 5.0);
    EXPECT_FALSE(frame.finished);
    EXPECT_TRUE(state.scrolling_out);
}

TEST(MarqueeTypesTest, HorizontalScroll_TransitionsToScrollingIn)
{
    HorizontalScrollState state;
    // Content "12" = 10 + 10 = 20 width. Speed 100, so 0.2s to scroll out.
    startHorizontalScroll(state, "12", 50.0, 100.0, 10, 2, 5);

    // After 0.25s, viewport_x would be 25, which exceeds content_width (20).
    auto frame = updateHorizontalScroll(state, "12", 0.25);

    EXPECT_FALSE(state.scrolling_out);
    // Should teleport to -visible_width.
    EXPECT_DOUBLE_EQ(state.viewport_x, -50.0);
    EXPECT_FALSE(frame.finished);
}

TEST(MarqueeTypesTest, HorizontalScroll_FinishesWhenBackToZero)
{
    HorizontalScrollState state;
    startHorizontalScroll(state, "12", 50.0, 100.0, 10, 2, 5);

    // Scroll out phase.
    updateHorizontalScroll(state, "12", 0.25);
    EXPECT_FALSE(state.scrolling_out);
    EXPECT_DOUBLE_EQ(state.viewport_x, -50.0);

    // Scroll in phase: need to go from -50 to 0 at speed 100, so 0.5s.
    auto frame = updateHorizontalScroll(state, "12", 0.5);

    EXPECT_DOUBLE_EQ(state.viewport_x, 0.0);
    EXPECT_TRUE(frame.finished);
}

TEST(MarqueeTypesTest, HorizontalScroll_FrameContainsDigits)
{
    HorizontalScrollState state;
    startHorizontalScroll(state, "12", 100.0, 50.0, 10, 2, 5);

    auto frame = updateHorizontalScroll(state, "12", 0.0);

    ASSERT_EQ(frame.digits.size(), 2u);
    EXPECT_EQ(frame.digits[0].c, '1');
    EXPECT_EQ(frame.digits[1].c, '2');
}

TEST(MarqueeTypesTest, HorizontalScroll_ClampsToZeroOnFinish)
{
    HorizontalScrollState state;
    startHorizontalScroll(state, "1", 10.0, 100.0, 5, 1, 1);

    // Force into scroll-in phase.
    updateHorizontalScroll(state, "1", 0.1);  // Passes content_width (5), teleports to -10.
    EXPECT_FALSE(state.scrolling_out);

    // Overshoot: 0.2s at 100 = 20 units, from -10 would be +10, but should clamp to 0.
    auto frame = updateHorizontalScroll(state, "1", 0.2);

    EXPECT_DOUBLE_EQ(state.viewport_x, 0.0);
    EXPECT_DOUBLE_EQ(frame.viewportX, 0.0);
    EXPECT_TRUE(frame.finished);
}
