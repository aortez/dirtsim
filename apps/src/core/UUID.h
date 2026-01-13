#pragma once

/**
 * \file
 * Simple UUID implementation for unique entity identification.
 * Uses version 4 (random) format per RFC 4122.
 */

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {

class UUID {
public:
    // Creates a nil UUID (all zeros).
    UUID();

    // Creates a new random UUID (version 4).
    static UUID generate();

    // Creates a nil UUID (all zeros).
    static UUID nil();

    // Parse from string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
    static UUID fromString(const std::string& str);

    // Full string representation.
    std::string toString() const;

    // First 8 hex chars for compact display.
    std::string toShortString() const;

    bool isNil() const;

    const std::array<uint8_t, 16>& bytes() const { return bytes_; }

    bool operator==(const UUID& other) const;
    bool operator!=(const UUID& other) const;
    bool operator<(const UUID& other) const;

    using serialize = zpp::bits::members<1>;

private:
    std::array<uint8_t, 16> bytes_;
};

} // namespace DirtSim

template <>
struct std::hash<DirtSim::UUID> {
    std::size_t operator()(const DirtSim::UUID& uuid) const noexcept;
};
