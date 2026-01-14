#include "core/scenarios/clock_scenario/MarqueeTypes.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

// Helper to create a width function for testing.
std::function<int(const std::string&)> makeTestWidthFunc(int digitWidth, int gap, int colonWidth)
{
    return [=](const std::string& c) -> int {
        if (c == ":") {
            return colonWidth;
        }
        if (c == " ") {
            return gap;
        }
        return digitWidth;
    };
}

} // namespace

// =============================================================================
// layoutString Tests
// =============================================================================

TEST(MarqueeTypesTest, LayoutString_SingleDigit)
{
    auto placements = layoutString("5", makeTestWidthFunc(5, 1, 1));

    ASSERT_EQ(placements.size(), 1u);
    EXPECT_EQ(placements[0].text, "5");
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_DOUBLE_EQ(placements[0].y, 0.0);
}

TEST(MarqueeTypesTest, LayoutString_TwoDigits)
{
    auto placements = layoutString("12", makeTestWidthFunc(5, 1, 1));

    ASSERT_EQ(placements.size(), 2u);
    EXPECT_EQ(placements[0].text, "1");
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_EQ(placements[1].text, "2");
    EXPECT_DOUBLE_EQ(placements[1].x, 5.0); // After first digit.
}

TEST(MarqueeTypesTest, LayoutString_DigitsWithSpace)
{
    auto placements = layoutString("1 2", makeTestWidthFunc(5, 2, 1));

    ASSERT_EQ(placements.size(), 2u);
    EXPECT_EQ(placements[0].text, "1");
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_EQ(placements[1].text, "2");
    EXPECT_DOUBLE_EQ(placements[1].x, 7.0); // 5 (digit) + 2 (gap).
}

TEST(MarqueeTypesTest, LayoutString_WithColon)
{
    auto placements = layoutString("1:2", makeTestWidthFunc(5, 1, 3));

    ASSERT_EQ(placements.size(), 3u);
    EXPECT_EQ(placements[0].text, "1");
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_EQ(placements[1].text, ":");
    EXPECT_DOUBLE_EQ(placements[1].x, 5.0);
    EXPECT_EQ(placements[2].text, "2");
    EXPECT_DOUBLE_EQ(placements[2].x, 8.0); // 5 + 3 (colon).
}

TEST(MarqueeTypesTest, LayoutString_TimeFormat)
{
    // Typical time format: "1 2 : 3 4".
    auto placements = layoutString("1 2 : 3 4", makeTestWidthFunc(5, 1, 2));

    ASSERT_EQ(placements.size(), 5u);

    // '1' at x=0.
    EXPECT_EQ(placements[0].text, "1");
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);

    // '2' at x=6 (digit=5 + gap=1).
    EXPECT_EQ(placements[1].text, "2");
    EXPECT_DOUBLE_EQ(placements[1].x, 6.0);

    // ':' at x=12 (6 + digit=5 + gap=1).
    EXPECT_EQ(placements[2].text, ":");
    EXPECT_DOUBLE_EQ(placements[2].x, 12.0);

    // '3' at x=15 (12 + colon=2 + gap=1).
    EXPECT_EQ(placements[3].text, "3");
    EXPECT_DOUBLE_EQ(placements[3].x, 15.0);

    // '4' at x=21 (15 + digit=5 + gap=1).
    EXPECT_EQ(placements[4].text, "4");
    EXPECT_DOUBLE_EQ(placements[4].x, 21.0);
}

TEST(MarqueeTypesTest, LayoutString_EmptyString)
{
    auto placements = layoutString("", makeTestWidthFunc(5, 1, 1));

    EXPECT_TRUE(placements.empty());
}

TEST(MarqueeTypesTest, LayoutString_OnlySpaces)
{
    auto placements = layoutString("   ", makeTestWidthFunc(5, 2, 1));

    // Spaces don't produce placements, just advance position.
    EXPECT_TRUE(placements.empty());
}

TEST(MarqueeTypesTest, LayoutString_AllDigits)
{
    auto placements = layoutString("0123456789", makeTestWidthFunc(5, 1, 1));

    ASSERT_EQ(placements.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(placements[i].text, std::string(1, '0' + i));
        EXPECT_DOUBLE_EQ(placements[i].x, i * 5.0);
    }
}

TEST(MarqueeTypesTest, LayoutString_Utf8MultiByteEmoji)
{
    // Test UTF-8 multi-byte character handling with emoji.
    // "🌞1" contains a 4-byte emoji followed by ASCII '1'.
    auto placements = layoutString("🌞1", makeTestWidthFunc(10, 2, 5));

    ASSERT_EQ(placements.size(), 2u);
    EXPECT_EQ(placements[0].text, "🌞"); // 4-byte UTF-8 sequence.
    EXPECT_DOUBLE_EQ(placements[0].x, 0.0);
    EXPECT_EQ(placements[1].text, "1");
    EXPECT_DOUBLE_EQ(placements[1].x, 10.0); // After emoji width.
}

// =============================================================================
// calculateStringWidth Tests
// =============================================================================

TEST(MarqueeTypesTest, CalculateWidth_SingleDigit)
{
    int width = calculateStringWidth("5", makeTestWidthFunc(5, 1, 1));
    EXPECT_EQ(width, 5);
}

TEST(MarqueeTypesTest, CalculateWidth_TwoDigits)
{
    int width = calculateStringWidth("12", makeTestWidthFunc(5, 1, 1));
    EXPECT_EQ(width, 10);
}

TEST(MarqueeTypesTest, CalculateWidth_DigitsWithSpace)
{
    int width = calculateStringWidth("1 2", makeTestWidthFunc(5, 2, 1));
    EXPECT_EQ(width, 12); // 5 + 2 + 5.
}

TEST(MarqueeTypesTest, CalculateWidth_WithColon)
{
    int width = calculateStringWidth("1:2", makeTestWidthFunc(5, 1, 3));
    EXPECT_EQ(width, 13); // 5 + 3 + 5.
}

TEST(MarqueeTypesTest, CalculateWidth_TimeFormat)
{
    // "1 2 : 3 4" = d + g + d + g + c + g + d + g + d.
    // = 5 + 1 + 5 + 1 + 2 + 1 + 5 + 1 + 5 = 26.
    int width = calculateStringWidth("1 2 : 3 4", makeTestWidthFunc(5, 1, 2));
    EXPECT_EQ(width, 26);
}

TEST(MarqueeTypesTest, CalculateWidth_EmptyString)
{
    int width = calculateStringWidth("", makeTestWidthFunc(5, 1, 1));
    EXPECT_EQ(width, 0);
}

TEST(MarqueeTypesTest, CalculateWidth_OnlySpaces)
{
    int width = calculateStringWidth("   ", makeTestWidthFunc(5, 2, 1));
    EXPECT_EQ(width, 6); // 3 gaps of 2 each.
}

TEST(MarqueeTypesTest, CalculateWidth_Utf8MultiByteEmoji)
{
    // "🌞1" = emoji (10) + digit (10) = 20.
    int width = calculateStringWidth("🌞1", makeTestWidthFunc(10, 2, 5));
    EXPECT_EQ(width, 20);
}

// =============================================================================
// HorizontalScroll Tests
// =============================================================================

TEST(MarqueeTypesTest, HorizontalScroll_StartInitializesState)
{
    HorizontalScrollState state;
    auto getWidth = makeTestWidthFunc(10, 2, 5);
    startHorizontalScroll(state, "12:34", 100.0, 50.0, getWidth);

    EXPECT_DOUBLE_EQ(state.viewport_x, 0.0);
    EXPECT_DOUBLE_EQ(state.visible_width, 100.0);
    EXPECT_DOUBLE_EQ(state.speed, 50.0);
    EXPECT_TRUE(state.scrolling_out);
    // Content width: 10 + 10 + 5 + 10 + 10 = 45.
    EXPECT_DOUBLE_EQ(state.content_width, 45.0);
}

TEST(MarqueeTypesTest, HorizontalScroll_UpdateAdvancesViewport)
{
    HorizontalScrollState state;
    auto getWidth = makeTestWidthFunc(10, 2, 5);
    startHorizontalScroll(state, "12", 100.0, 50.0, getWidth);

    auto frame = updateHorizontalScroll(state, "12", 0.1, getWidth);

    // After 0.1s at 50 units/s, viewport should advance 5 units.
    EXPECT_DOUBLE_EQ(state.viewport_x, 5.0);
    EXPECT_DOUBLE_EQ(frame.viewportX, 5.0);
    EXPECT_FALSE(frame.finished);
    EXPECT_TRUE(state.scrolling_out);
}

TEST(MarqueeTypesTest, HorizontalScroll_TransitionsToScrollingIn)
{
    HorizontalScrollState state;
    auto getWidth = makeTestWidthFunc(10, 2, 5);
    // Content "12" = 10 + 10 = 20 width. Speed 100, so 0.2s to scroll out.
    startHorizontalScroll(state, "12", 50.0, 100.0, getWidth);

    // After 0.25s, viewport_x would be 25, which exceeds content_width (20).
    auto frame = updateHorizontalScroll(state, "12", 0.25, getWidth);

    EXPECT_FALSE(state.scrolling_out);
    // Should teleport to -visible_width.
    EXPECT_DOUBLE_EQ(state.viewport_x, -50.0);
    EXPECT_FALSE(frame.finished);
}

TEST(MarqueeTypesTest, HorizontalScroll_FinishesWhenBackToZero)
{
    HorizontalScrollState state;
    auto getWidth = makeTestWidthFunc(10, 2, 5);
    startHorizontalScroll(state, "12", 50.0, 100.0, getWidth);

    // Scroll out phase.
    updateHorizontalScroll(state, "12", 0.25, getWidth);
    EXPECT_FALSE(state.scrolling_out);
    EXPECT_DOUBLE_EQ(state.viewport_x, -50.0);

    // Scroll in phase: need to go from -50 to 0 at speed 100, so 0.5s.
    auto frame = updateHorizontalScroll(state, "12", 0.5, getWidth);

    EXPECT_DOUBLE_EQ(state.viewport_x, 0.0);
    EXPECT_TRUE(frame.finished);
}

TEST(MarqueeTypesTest, HorizontalScroll_FrameContainsDigits)
{
    HorizontalScrollState state;
    auto getWidth = makeTestWidthFunc(10, 2, 5);
    startHorizontalScroll(state, "12", 100.0, 50.0, getWidth);

    auto frame = updateHorizontalScroll(state, "12", 0.0, getWidth);

    ASSERT_EQ(frame.placements.size(), 2u);
    EXPECT_EQ(frame.placements[0].text, "1");
    EXPECT_EQ(frame.placements[1].text, "2");
}

TEST(MarqueeTypesTest, HorizontalScroll_ClampsToZeroOnFinish)
{
    HorizontalScrollState state;
    auto getWidth = makeTestWidthFunc(5, 1, 1);
    startHorizontalScroll(state, "1", 10.0, 100.0, getWidth);

    // Force into scroll-in phase.
    updateHorizontalScroll(
        state, "1", 0.1, getWidth); // Passes content_width (5), teleports to -10.
    EXPECT_FALSE(state.scrolling_out);

    // Overshoot: 0.2s at 100 = 20 units, from -10 would be +10, but should clamp to 0.
    auto frame = updateHorizontalScroll(state, "1", 0.2, getWidth);

    EXPECT_DOUBLE_EQ(state.viewport_x, 0.0);
    EXPECT_DOUBLE_EQ(frame.viewportX, 0.0);
    EXPECT_TRUE(frame.finished);
}

// =============================================================================
// VerticalSlide Tests
// =============================================================================

TEST(MarqueeTypesTest, VerticalSlide_InitializesState)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    EXPECT_DOUBLE_EQ(state.speed, 2.0);
    EXPECT_EQ(state.digit_height, 15);
    EXPECT_FALSE(state.active);
    EXPECT_TRUE(state.changing_digits.empty());
}

TEST(MarqueeTypesTest, VerticalSlide_NoChangeDoesNotStartAnimation)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    bool started = checkAndStartSlide(state, "1 2 : 3 4", "1 2 : 3 4");

    EXPECT_FALSE(started);
    EXPECT_FALSE(state.active);
}

TEST(MarqueeTypesTest, VerticalSlide_ChangeStartsAnimation)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    bool started = checkAndStartSlide(state, "1 2 : 3 4", "1 2 : 3 5");

    EXPECT_TRUE(started);
    EXPECT_TRUE(state.active);
    EXPECT_EQ(state.changing_digits.size(), 1u); // Only the last digit changed.
}

TEST(MarqueeTypesTest, VerticalSlide_TracksCorrectChangingDigits)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    // Change from "1 2 : 3 4" to "1 2 : 4 5" (two digits change).
    bool started = checkAndStartSlide(state, "1 2 : 3 4", "1 2 : 4 5");

    EXPECT_TRUE(started);
    EXPECT_EQ(state.changing_digits.size(), 2u);

    // Find the changed digits.
    bool found_3_to_4 = false;
    bool found_4_to_5 = false;
    for (const auto& slide : state.changing_digits) {
        if (slide.old_char == '3' && slide.new_char == '4') {
            found_3_to_4 = true;
        }
        if (slide.old_char == '4' && slide.new_char == '5') {
            found_4_to_5 = true;
        }
    }
    EXPECT_TRUE(found_3_to_4);
    EXPECT_TRUE(found_4_to_5);
}

TEST(MarqueeTypesTest, VerticalSlide_UpdateAdvancesProgress)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    checkAndStartSlide(state, "1 2 : 3 4", "1 2 : 3 5");
    ASSERT_TRUE(state.active);
    ASSERT_EQ(state.changing_digits.size(), 1u);
    EXPECT_DOUBLE_EQ(state.changing_digits[0].progress, 0.0);

    // Update with 0.25s at speed 2.0 = 0.5 progress.
    auto getWidth = makeTestWidthFunc(10, 2, 5);
    auto frame = updateVerticalSlide(state, 0.25, getWidth);

    EXPECT_DOUBLE_EQ(state.changing_digits[0].progress, 0.5);
    EXPECT_FALSE(frame.finished);
    EXPECT_TRUE(state.active);
}

TEST(MarqueeTypesTest, VerticalSlide_CompletesWhenProgressReachesOne)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    checkAndStartSlide(state, "1 2 : 3 4", "1 2 : 3 5");

    // Update with 0.6s at speed 2.0 = 1.2 progress (clamped to 1.0).
    auto getWidth = makeTestWidthFunc(10, 2, 5);
    auto frame = updateVerticalSlide(state, 0.6, getWidth);

    EXPECT_DOUBLE_EQ(state.changing_digits[0].progress, 1.0);
    EXPECT_TRUE(frame.finished);
    EXPECT_FALSE(state.active);
}

TEST(MarqueeTypesTest, VerticalSlide_DoesNotInterruptOngoingAnimation)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    // Start first animation.
    bool started1 = checkAndStartSlide(state, "1 2 : 3 4", "1 2 : 3 5");
    EXPECT_TRUE(started1);

    // Try to start another animation while first is active.
    bool started2 = checkAndStartSlide(state, "1 2 : 3 5", "1 2 : 3 6");
    EXPECT_FALSE(started2); // Should be rejected.

    // Original animation state should be unchanged.
    EXPECT_EQ(state.changing_digits.size(), 1u);
    EXPECT_EQ(state.changing_digits[0].old_char, '4');
    EXPECT_EQ(state.changing_digits[0].new_char, '5');
}

TEST(MarqueeTypesTest, VerticalSlide_FrameContainsStaticAndAnimatingDigits)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    // "1 2" -> "1 3" (only second digit changes).
    checkAndStartSlide(state, "1 2", "1 3");

    auto getWidth = makeTestWidthFunc(10, 2, 5);
    auto frame = updateVerticalSlide(state, 0.0, getWidth);

    // Should contain: static '1', animating '2' (old) and '3' (new).
    // Total: 3 placements.
    EXPECT_GE(frame.placements.size(), 2u); // At least static '1' and new '3'.

    // Find the static digit.
    bool found_static_1 = false;
    for (const auto& p : frame.placements) {
        if (p.text == "1" && p.y == 0.0) {
            found_static_1 = true;
        }
    }
    EXPECT_TRUE(found_static_1);
}

TEST(MarqueeTypesTest, VerticalSlide_AnimatingDigitsHaveOffsetY)
{
    VerticalSlideState state;
    initVerticalSlide(state, 2.0, 15);

    checkAndStartSlide(state, "1 2", "1 3");

    auto getWidth = makeTestWidthFunc(10, 2, 5);
    // At progress 0.5, old digit should be halfway down, new digit halfway in.
    updateVerticalSlide(state, 0.25, getWidth); // 0.5 progress.

    auto frame = updateVerticalSlide(state, 0.0, getWidth); // Don't advance, just get frame.

    // Look for the animating digits.
    bool found_old_at_offset = false;
    bool found_new_at_offset = false;
    double digit_height = 15.0;
    double expected_old_y = 0.5 * digit_height;                 // 7.5
    double expected_new_y = -digit_height + 0.5 * digit_height; // -7.5

    for (const auto& p : frame.placements) {
        if (p.text == "2" && std::abs(p.y - expected_old_y) < 0.01) {
            found_old_at_offset = true;
        }
        if (p.text == "3" && std::abs(p.y - expected_new_y) < 0.01) {
            found_new_at_offset = true;
        }
    }
    EXPECT_TRUE(found_old_at_offset) << "Old digit '2' should be at y=" << expected_old_y;
    EXPECT_TRUE(found_new_at_offset) << "New digit '3' should be at y=" << expected_new_y;
}
