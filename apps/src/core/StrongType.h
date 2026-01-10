#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <ostream>
#include <spdlog/fmt/fmt.h>
#include <zpp_bits.h>

// A simple strong type wrapper for integers.
// Creates distinct types that prevent accidental mixing of values.
//
// Usage:
//   using UserId = StrongType<struct UserIdTag>;
//   using GroupId = StrongType<struct GroupIdTag>;
//
//   UserId id{42};
//   GroupId group{42};
//   // id == group;  // Compile error - different types.
//   // id + 1;       // Compile error - no implicit arithmetic.
//   int raw = id.get();  // Explicit access to underlying value.

template <typename Tag>
class StrongType {
public:
    constexpr StrongType() : m_value{ 0 } {}
    constexpr explicit StrongType(int value) : m_value{ value } {}

    [[nodiscard]] constexpr int get() const { return m_value; }

    // Comparison operators.
    constexpr bool operator==(const StrongType& other) const { return m_value == other.m_value; }
    constexpr bool operator!=(const StrongType& other) const { return m_value != other.m_value; }
    constexpr bool operator<(const StrongType& other) const { return m_value < other.m_value; }
    constexpr bool operator<=(const StrongType& other) const { return m_value <= other.m_value; }
    constexpr bool operator>(const StrongType& other) const { return m_value > other.m_value; }
    constexpr bool operator>=(const StrongType& other) const { return m_value >= other.m_value; }

    // Increment operators (for ID generation).
    StrongType& operator++()
    {
        ++m_value;
        return *this;
    }
    StrongType operator++(int)
    {
        StrongType temp = *this;
        ++m_value;
        return temp;
    }

    // Binary serialization support for zpp_bits requires public member access.
    using serialize = zpp::bits::members<1>;
    int m_value;
};

// Hash support for use in unordered containers.
template <typename Tag>
struct std::hash<StrongType<Tag>> {
    std::size_t operator()(const StrongType<Tag>& st) const noexcept
    {
        return std::hash<int>{}(st.get());
    }
};

// JSON serialization support for nlohmann::json.
template <typename Tag>
void to_json(nlohmann::json& j, const StrongType<Tag>& st)
{
    j = st.get();
}

template <typename Tag>
void from_json(const nlohmann::json& j, StrongType<Tag>& st)
{
    st = StrongType<Tag>{ j.get<int>() };
}

// fmt formatting support for spdlog.
template <typename Tag>
struct fmt::formatter<StrongType<Tag>> : fmt::formatter<int> {
    auto format(const StrongType<Tag>& st, fmt::format_context& ctx) const
    {
        return fmt::formatter<int>::format(st.get(), ctx);
    }
};

// Stream operator for std::cout and similar.
template <typename Tag>
std::ostream& operator<<(std::ostream& os, const StrongType<Tag>& st)
{
    return os << st.get();
}
