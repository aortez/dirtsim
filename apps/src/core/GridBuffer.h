#pragma once

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

namespace DirtSim {

/**
 * Generic 2D grid buffer for cache-friendly SoA (Structure of Arrays) storage.
 * Use this for parallel arrays that need efficient row-major traversal.
 */
template <typename T>
struct GridBuffer {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<T> data;

    void resize(uint32_t w, uint32_t h, T default_value = T{})
    {
        width = w;
        height = h;
        data.resize(static_cast<size_t>(w) * h, default_value);
    }

    void clear(T value = T{}) { std::fill(data.begin(), data.end(), value); }

    T at(uint32_t x, uint32_t y) const { return data[y * width + x]; }

    T& at(uint32_t x, uint32_t y) { return data[y * width + x]; }

    void set(uint32_t x, uint32_t y, T value) { data[y * width + x] = value; }

    // Direct row access for tight loops.
    T* row(uint32_t y) { return &data[y * width]; }

    const T* row(uint32_t y) const { return &data[y * width]; }

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
