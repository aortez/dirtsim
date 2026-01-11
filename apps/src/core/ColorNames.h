#pragma once
#include <cstdint>

// Named color constants and RGBA utilities.
// Color values defined in ColorNames.cpp to minimize rebuild impact.
namespace ColorNames {

// --- RGBA Utilities (inlined) ---

// Pack components (0-255) into RGBA.
inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16)
        | (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

// Pack float components (0.0-1.0) into RGBA.
inline uint32_t rgbaF(float r, float g, float b, float a = 1.0f)
{
    return rgba(
        static_cast<uint8_t>(r * 255.0f),
        static_cast<uint8_t>(g * 255.0f),
        static_cast<uint8_t>(b * 255.0f),
        static_cast<uint8_t>(a * 255.0f));
}

// Extract components (0-255).
inline uint8_t getR(uint32_t color)
{
    return (color >> 24) & 0xFF;
}
inline uint8_t getG(uint32_t color)
{
    return (color >> 16) & 0xFF;
}
inline uint8_t getB(uint32_t color)
{
    return (color >> 8) & 0xFF;
}
inline uint8_t getA(uint32_t color)
{
    return color & 0xFF;
}

// Extract components as float (0.0-1.0).
inline float getRf(uint32_t color)
{
    return getR(color) / 255.0f;
}
inline float getGf(uint32_t color)
{
    return getG(color) / 255.0f;
}
inline float getBf(uint32_t color)
{
    return getB(color) / 255.0f;
}
inline float getAf(uint32_t color)
{
    return getA(color) / 255.0f;
}

// Linear interpolation between two colors.
inline uint32_t lerp(uint32_t a, uint32_t b, float t)
{
    float r = getRf(a) + (getRf(b) - getRf(a)) * t;
    float g = getGf(a) + (getGf(b) - getGf(a)) * t;
    float blue = getBf(a) + (getBf(b) - getBf(a)) * t;
    float alpha = getAf(a) + (getAf(b) - getAf(a)) * t;
    return rgbaF(r, g, blue, alpha);
}

// Multiply color by scalar (for intensity), clamped to prevent overflow.
inline uint32_t scale(uint32_t color, float s)
{
    auto clamp = [](float v) { return v > 1.0f ? 1.0f : (v < 0.0f ? 0.0f : v); };
    return rgbaF(
        clamp(getRf(color) * s), clamp(getGf(color) * s), clamp(getBf(color) * s), getAf(color));
}

// Multiply two colors component-wise (for tinting).
inline uint32_t multiply(uint32_t a, uint32_t b)
{
    return rgbaF(
        getRf(a) * getRf(b), getGf(a) * getGf(b), getBf(a) * getBf(b), getAf(a) * getAf(b));
}

// Add two colors (clamped).
inline uint32_t add(uint32_t a, uint32_t b)
{
    auto clamp = [](float v) { return v > 1.0f ? 1.0f : v; };
    return rgbaF(
        clamp(getRf(a) + getRf(b)),
        clamp(getGf(a) + getGf(b)),
        clamp(getBf(a) + getBf(b)),
        clamp(getAf(a) + getAf(b)));
}

// Get perceived brightness (0.0-1.0) using luminance formula.
inline float brightness(uint32_t color)
{
    return 0.299f * getRf(color) + 0.587f * getGf(color) + 0.114f * getBf(color);
}

// --- Named Colors (values in .cpp) ---

// Light sources.
uint32_t warmSunlight();
uint32_t coolMoonlight();
uint32_t torchOrange();
uint32_t candleYellow();

// Ambient presets.
uint32_t dayAmbient();
uint32_t duskAmbient();
uint32_t nightAmbient();
uint32_t caveAmbient();

// Material base colors.
uint32_t air();
uint32_t dirt();
uint32_t leaf();
uint32_t metal();
uint32_t root();
uint32_t sand();
uint32_t seed();
uint32_t stone();
uint32_t water();
uint32_t wood();

// Material emissions.
uint32_t lavaGlow();
uint32_t seedGlow();

// Utility.
uint32_t white();
uint32_t black();
uint32_t transparent();

} // namespace ColorNames
