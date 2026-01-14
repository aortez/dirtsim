#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim::RenderFormat {

enum class EnumType : uint8_t {
    Basic = 0,
    Debug = 1,
};

std::string toString(EnumType format);

std::optional<EnumType> fromString(const std::string& str);

} // namespace DirtSim::RenderFormat
