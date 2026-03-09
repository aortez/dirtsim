#include "ClockTimeResolver.h"

#include <chrono>

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
#define DIRTSIM_HAS_CHRONO_TZDB 1
#else
#define DIRTSIM_HAS_CHRONO_TZDB 0
#endif

namespace DirtSim::ClockTimeResolver {

namespace {

using TimePoint = std::chrono::system_clock::time_point;
constexpr std::chrono::weekday kSunday{ 0 };

std::tm toLocalTm(const TimePoint timePoint)
{
    const std::time_t timeValue = std::chrono::system_clock::to_time_t(timePoint);
    std::tm result{};
    localtime_r(&timeValue, &result);
    return result;
}

std::tm toUtcTm(const TimePoint timePoint)
{
    const std::time_t timeValue = std::chrono::system_clock::to_time_t(timePoint);
    std::tm result{};
    gmtime_r(&timeValue, &result);
    return result;
}

int extractYear(const TimePoint timePoint)
{
    const auto day = std::chrono::floor<std::chrono::days>(timePoint);
    const std::chrono::year_month_day ymd{ day };
    return static_cast<int>(ymd.year());
}

std::chrono::sys_days firstSundayInMonth(const int year, const unsigned monthValue)
{
    return std::chrono::sys_days(
        std::chrono::year_month_weekday{
            std::chrono::year{ year }, std::chrono::month{ monthValue }, kSunday[1] });
}

std::chrono::sys_days nthSundayInMonth(
    const int year, const unsigned monthValue, const unsigned occurrence)
{
    return std::chrono::sys_days(
        std::chrono::year_month_weekday{
            std::chrono::year{ year }, std::chrono::month{ monthValue }, kSunday[occurrence] });
}

std::chrono::sys_days lastSundayInMonth(const int year, const unsigned monthValue)
{
    return std::chrono::sys_days(
        std::chrono::year_month_weekday_last{ std::chrono::year{ year },
                                              std::chrono::month{ monthValue },
                                              std::chrono::weekday_last{ kSunday } });
}

TimePoint resolveUtcTransition(
    const std::chrono::sys_days localDay,
    const std::chrono::hours localHour,
    const std::chrono::minutes localOffset)
{
    return localDay + localHour - localOffset;
}

bool isUsDstActive(const ClockTimezones::Info& info, const TimePoint now)
{
    const int year = extractYear(now);
    const TimePoint start = resolveUtcTransition(
        nthSundayInMonth(year, 3, 2),
        std::chrono::hours{ 2 },
        std::chrono::minutes{ info.standardOffsetMinutes });
    const TimePoint end = resolveUtcTransition(
        firstSundayInMonth(year, 11),
        std::chrono::hours{ 2 },
        std::chrono::minutes{ info.daylightOffsetMinutes });
    return now >= start && now < end;
}

bool isEuropeanDstActive(const int year, const TimePoint now)
{
    const TimePoint start = lastSundayInMonth(year, 3) + std::chrono::hours{ 1 };
    const TimePoint end = lastSundayInMonth(year, 10) + std::chrono::hours{ 1 };
    return now >= start && now < end;
}

bool isAustraliaSydneyDstActive(const ClockTimezones::Info& info, const TimePoint now)
{
    const int year = extractYear(now);
    const TimePoint end = resolveUtcTransition(
        firstSundayInMonth(year, 4),
        std::chrono::hours{ 3 },
        std::chrono::minutes{ info.daylightOffsetMinutes });
    if (now < end) {
        return true;
    }

    const TimePoint start = resolveUtcTransition(
        firstSundayInMonth(year, 10),
        std::chrono::hours{ 2 },
        std::chrono::minutes{ info.standardOffsetMinutes });
    return now >= start;
}

bool isDstActive(const ClockTimezones::Info& info, const TimePoint now)
{
    switch (info.dstRule) {
        case ClockTimezones::DstRuleFamily::None:
            return false;
        case ClockTimezones::DstRuleFamily::AustraliaSydney:
            return isAustraliaSydneyDstActive(info, now);
        case ClockTimezones::DstRuleFamily::Europe:
            return isEuropeanDstActive(extractYear(now), now);
        case ClockTimezones::DstRuleFamily::UnitedStates:
            return isUsDstActive(info, now);
    }

    return false;
}

int resolveOffsetMinutesFallback(const ClockTimezones::Info& info, const TimePoint now)
{
    if (isDstActive(info, now)) {
        return info.daylightOffsetMinutes;
    }

    return info.standardOffsetMinutes;
}

std::optional<std::tm> tryResolveWithTzdb(const ClockTimezones::Info& info, const TimePoint now)
{
#if DIRTSIM_HAS_CHRONO_TZDB
    if (!info.ianaName) {
        return std::nullopt;
    }

    try {
        const std::chrono::time_zone* zone = std::chrono::locate_zone(info.ianaName);
        const std::chrono::sys_info sysInfo = zone->get_info(now);
        std::tm result = toUtcTm(now + sysInfo.offset);
        result.tm_isdst = sysInfo.save != decltype(sysInfo.save)::zero();
        return result;
    }
    catch (...) {
        return std::nullopt;
    }
#else
    (void)info;
    (void)now;
    return std::nullopt;
#endif
}

} // namespace

std::tm resolveClockTime(const Config::ClockTimezone timezone, const TimePoint now)
{
    if (timezone == Config::ClockTimezone::Local) {
        return toLocalTm(now);
    }

    const ClockTimezones::Info* info = ClockTimezones::find(timezone);
    if (!info) {
        return toLocalTm(now);
    }

    if (auto tzdbResult = tryResolveWithTzdb(*info, now)) {
        return *tzdbResult;
    }

    const int offsetMinutes = resolveOffsetMinutesFallback(*info, now);
    std::tm result = toUtcTm(now + std::chrono::minutes{ offsetMinutes });
    result.tm_isdst = offsetMinutes != info->standardOffsetMinutes;
    return result;
}

} // namespace DirtSim::ClockTimeResolver
