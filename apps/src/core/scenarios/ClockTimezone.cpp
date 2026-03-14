#include "ClockTimezone.h"

#include <array>

namespace DirtSim::ClockTimezones {

namespace {

constexpr std::array<Info, 10> TIMEZONES = { {
    {
        .timezone = Config::ClockTimezone::Local,
        .label = "Local System Time",
        .ianaName = nullptr,
        .standardOffsetMinutes = 0,
        .daylightOffsetMinutes = 0,
        .dstRule = DstRuleFamily::None,
    },
    {
        .timezone = Config::ClockTimezone::LosAngeles,
        .label = "Los Angeles",
        .ianaName = "America/Los_Angeles",
        .standardOffsetMinutes = -8 * 60,
        .daylightOffsetMinutes = -7 * 60,
        .dstRule = DstRuleFamily::UnitedStates,
    },
    {
        .timezone = Config::ClockTimezone::Denver,
        .label = "Denver",
        .ianaName = "America/Denver",
        .standardOffsetMinutes = -7 * 60,
        .daylightOffsetMinutes = -6 * 60,
        .dstRule = DstRuleFamily::UnitedStates,
    },
    {
        .timezone = Config::ClockTimezone::Chicago,
        .label = "Chicago",
        .ianaName = "America/Chicago",
        .standardOffsetMinutes = -6 * 60,
        .daylightOffsetMinutes = -5 * 60,
        .dstRule = DstRuleFamily::UnitedStates,
    },
    {
        .timezone = Config::ClockTimezone::NewYork,
        .label = "New York",
        .ianaName = "America/New_York",
        .standardOffsetMinutes = -5 * 60,
        .daylightOffsetMinutes = -4 * 60,
        .dstRule = DstRuleFamily::UnitedStates,
    },
    {
        .timezone = Config::ClockTimezone::UTC,
        .label = "UTC",
        .ianaName = "UTC",
        .standardOffsetMinutes = 0,
        .daylightOffsetMinutes = 0,
        .dstRule = DstRuleFamily::None,
    },
    {
        .timezone = Config::ClockTimezone::London,
        .label = "London",
        .ianaName = "Europe/London",
        .standardOffsetMinutes = 0,
        .daylightOffsetMinutes = 60,
        .dstRule = DstRuleFamily::Europe,
    },
    {
        .timezone = Config::ClockTimezone::Paris,
        .label = "Paris",
        .ianaName = "Europe/Paris",
        .standardOffsetMinutes = 60,
        .daylightOffsetMinutes = 120,
        .dstRule = DstRuleFamily::Europe,
    },
    {
        .timezone = Config::ClockTimezone::Tokyo,
        .label = "Tokyo",
        .ianaName = "Asia/Tokyo",
        .standardOffsetMinutes = 9 * 60,
        .daylightOffsetMinutes = 9 * 60,
        .dstRule = DstRuleFamily::None,
    },
    {
        .timezone = Config::ClockTimezone::Sydney,
        .label = "Sydney",
        .ianaName = "Australia/Sydney",
        .standardOffsetMinutes = 10 * 60,
        .daylightOffsetMinutes = 11 * 60,
        .dstRule = DstRuleFamily::AustraliaSydney,
    },
} };

} // namespace

std::span<const Info> all()
{
    return TIMEZONES;
}

const Info* find(const Config::ClockTimezone timezone)
{
    for (const Info& info : TIMEZONES) {
        if (info.timezone == timezone) {
            return &info;
        }
    }

    return nullptr;
}

bool isValid(const Config::ClockTimezone timezone)
{
    return find(timezone) != nullptr;
}

} // namespace DirtSim::ClockTimezones

namespace DirtSim::Config {

const char* getDisplayName(const ClockTimezone timezone)
{
    const ClockTimezones::Info* info = ClockTimezones::find(timezone);
    if (!info) {
        return "Unknown";
    }

    return info->label;
}

} // namespace DirtSim::Config
