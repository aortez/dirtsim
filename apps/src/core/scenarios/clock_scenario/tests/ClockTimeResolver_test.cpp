#include "core/scenarios/clock_scenario/ClockTimeResolver.h"

#include <chrono>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

std::chrono::system_clock::time_point makeUtcTime(
    const int year,
    const unsigned month,
    const unsigned day,
    const int hour,
    const int minute,
    const int second = 0)
{
    return std::chrono::sys_days{ std::chrono::year{ year } / std::chrono::month{ month }
                                  / std::chrono::day{ day } }
    + std::chrono::hours{ hour } + std::chrono::minutes{ minute } + std::chrono::seconds{ second };
}

void expectTime(
    const std::tm& timeInfo,
    const int year,
    const int month,
    const int day,
    const int hour,
    const int minute,
    const int second,
    const int isDst)
{
    EXPECT_EQ(timeInfo.tm_year + 1900, year);
    EXPECT_EQ(timeInfo.tm_mon + 1, month);
    EXPECT_EQ(timeInfo.tm_mday, day);
    EXPECT_EQ(timeInfo.tm_hour, hour);
    EXPECT_EQ(timeInfo.tm_min, minute);
    EXPECT_EQ(timeInfo.tm_sec, second);
    EXPECT_EQ(timeInfo.tm_isdst, isDst);
}

} // namespace

TEST(ClockTimeResolverTest, LosAngelesSpringForwardUsesDstOffset)
{
    const std::tm before = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::LosAngeles, makeUtcTime(2026, 3, 8, 9, 59));
    expectTime(before, 2026, 3, 8, 1, 59, 0, 0);

    const std::tm after = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::LosAngeles, makeUtcTime(2026, 3, 8, 10, 0));
    expectTime(after, 2026, 3, 8, 3, 0, 0, 1);
}

TEST(ClockTimeResolverTest, LondonSpringForwardUsesEuropeanDstRule)
{
    const std::tm before = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::London, makeUtcTime(2026, 3, 29, 0, 59));
    expectTime(before, 2026, 3, 29, 0, 59, 0, 0);

    const std::tm after = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::London, makeUtcTime(2026, 3, 29, 1, 0));
    expectTime(after, 2026, 3, 29, 2, 0, 0, 1);
}

TEST(ClockTimeResolverTest, ParisSpringForwardUsesEuropeanDstRule)
{
    const std::tm before = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::Paris, makeUtcTime(2026, 3, 29, 0, 59));
    expectTime(before, 2026, 3, 29, 1, 59, 0, 0);

    const std::tm after = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::Paris, makeUtcTime(2026, 3, 29, 1, 0));
    expectTime(after, 2026, 3, 29, 3, 0, 0, 1);
}

TEST(ClockTimeResolverTest, SydneyFallBackUsesSouthernHemisphereDstRule)
{
    const std::tm before = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::Sydney, makeUtcTime(2026, 4, 4, 15, 59));
    expectTime(before, 2026, 4, 5, 2, 59, 0, 1);

    const std::tm after = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::Sydney, makeUtcTime(2026, 4, 4, 16, 0));
    expectTime(after, 2026, 4, 5, 2, 0, 0, 0);
}

TEST(ClockTimeResolverTest, SydneySpringForwardUsesSouthernHemisphereDstRule)
{
    const std::tm before = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::Sydney, makeUtcTime(2026, 10, 3, 15, 59));
    expectTime(before, 2026, 10, 4, 1, 59, 0, 0);

    const std::tm after = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::Sydney, makeUtcTime(2026, 10, 3, 16, 0));
    expectTime(after, 2026, 10, 4, 3, 0, 0, 1);
}

TEST(ClockTimeResolverTest, TokyoDoesNotApplyDst)
{
    const std::tm resolved = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::Tokyo, makeUtcTime(2026, 3, 8, 10, 0));
    expectTime(resolved, 2026, 3, 8, 19, 0, 0, 0);
}

TEST(ClockTimeResolverTest, UtcDoesNotApplyDst)
{
    const std::tm resolved = ClockTimeResolver::resolveClockTime(
        Config::ClockTimezone::UTC, makeUtcTime(2026, 3, 8, 10, 0));
    expectTime(resolved, 2026, 3, 8, 10, 0, 0, 0);
}
