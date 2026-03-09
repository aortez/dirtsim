#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace DirtSim::Config {

enum class ClockTimezone : uint8_t {
    Local = 0,
    LosAngeles,
    Denver,
    Chicago,
    NewYork,
    UTC,
    London,
    Paris,
    Tokyo,
    Sydney,
};

const char* getDisplayName(ClockTimezone timezone);

} // namespace DirtSim::Config

namespace DirtSim::ClockTimezones {

enum class DstRuleFamily : uint8_t {
    None = 0,
    AustraliaSydney,
    Europe,
    UnitedStates,
};

struct Info {
    Config::ClockTimezone timezone;
    const char* label;
    const char* ianaName;
    int standardOffsetMinutes;
    int daylightOffsetMinutes;
    DstRuleFamily dstRule;
};

std::span<const Info> all();
const Info* find(Config::ClockTimezone timezone);
bool isValid(Config::ClockTimezone timezone);

} // namespace DirtSim::ClockTimezones
