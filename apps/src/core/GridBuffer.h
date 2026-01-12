#pragma once

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {

/**
 * Generic 2D grid buffer for cache-friendly SoA (Structure of Arrays) storage.
 * Use this for parallel arrays that need efficient row-major traversal.
 */
template <typename T>
struct GridBuffer {
    int16_t width = 0;
    int16_t height = 0;
    std::vector<T> data;

    constexpr static auto serialize(auto& archive, auto& self)
    {
        return archive(self.width, self.height, self.data);
    }

    void resize(int w, int h, T default_value = T{})
    {
        width = static_cast<int16_t>(w);
        height = static_cast<int16_t>(h);
        data.resize(static_cast<size_t>(w) * h, default_value);
    }

    void clear(T value = T{}) { std::fill(data.begin(), data.end(), value); }

    T at(int x, int y) const { return data[static_cast<size_t>(y) * width + x]; }

    T& at(int x, int y) { return data[static_cast<size_t>(y) * width + x]; }

    void set(int x, int y, T value) { data[static_cast<size_t>(y) * width + x] = value; }

    // Direct row access for tight loops.
    T* row(int y) { return &data[static_cast<size_t>(y) * width]; }

    const T* row(int y) const { return &data[static_cast<size_t>(y) * width]; }

    // Direct data access for bulk operations.
    T* begin() { return data.data(); }

    const T* begin() const { return data.data(); }

    T* end() { return data.data() + data.size(); }

    const T* end() const { return data.data() + data.size(); }

    size_t size() const { return data.size(); }
};

// JSON serialization for GridBuffer (runtime-only field, serializes as empty object).
template <typename T>
inline void to_json(nlohmann::json& j, const GridBuffer<T>& /*buffer*/)
{
    // Runtime-only buffer - serialize as null to minimize payload.
    j = nullptr;
}

template <typename T>
inline void from_json(const nlohmann::json& /*j*/, GridBuffer<T>& /*buffer*/)
{
    // Runtime-only buffer - skip deserialization. Buffer will be resized when needed.
}

} // namespace DirtSim
