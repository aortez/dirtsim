#include "core/FontSampler.h"

#include <gtest/gtest.h>
#include <lvgl.h>
#include <map>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace {

class FontSamplerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Initialize LVGL for headless testing.
        lv_init();

        // Create a dummy display (required for canvas creation).
        display_ = lv_display_create(100, 100);
    }

    void TearDown() override
    {
        if (display_) {
            lv_display_delete(display_);
        }
        lv_deinit();
    }

    lv_display_t* display_ = nullptr;
};

TEST_F(FontSamplerTest, SampleDigitZero_ReturnsNonEmptyPattern)
{
    FontSampler sampler(&lv_font_montserrat_24, 12, 18);
    auto pattern = sampler.sampleCharacter('0');

    ASSERT_EQ(pattern.size(), 18);
    ASSERT_EQ(pattern[0].size(), 12);

    // Should have some filled pixels for '0'.
    int filledCount = 0;
    for (const auto& row : pattern) {
        for (bool cell : row) {
            if (cell) {
                filledCount++;
            }
        }
    }

    EXPECT_GT(filledCount, 10) << "Digit '0' should have filled pixels";
}

TEST_F(FontSamplerTest, SampleSpace_ReturnsEmptyPattern)
{
    FontSampler sampler(&lv_font_montserrat_24, 12, 18);
    auto pattern = sampler.sampleCharacter(' ');

    // Space should have no filled pixels.
    int filledCount = 0;
    for (const auto& row : pattern) {
        for (bool cell : row) {
            if (cell) {
                filledCount++;
            }
        }
    }

    EXPECT_EQ(filledCount, 0) << "Space should have no filled pixels";
}

TEST_F(FontSamplerTest, CachingWorks)
{
    FontSampler sampler(&lv_font_montserrat_24, 12, 18);

    // First access samples and caches.
    const auto& pattern1 = sampler.getCachedPattern('A');

    // Second access returns same cached reference.
    const auto& pattern2 = sampler.getCachedPattern('A');

    EXPECT_EQ(&pattern1, &pattern2) << "Cached pattern should return same reference";
}

TEST_F(FontSamplerTest, PrecacheAscii_CachesAllPrintableCharacters)
{
    FontSampler sampler(&lv_font_montserrat_24, 12, 18);
    sampler.precacheAscii();

    // Verify some characters are cached by checking they return patterns.
    const auto& patternA = sampler.getCachedPattern('A');
    const auto& patternZ = sampler.getCachedPattern('Z');
    const auto& pattern0 = sampler.getCachedPattern('0');

    EXPECT_FALSE(patternA.empty());
    EXPECT_FALSE(patternZ.empty());
    EXPECT_FALSE(pattern0.empty());
}

TEST_F(FontSamplerTest, ThresholdAffectsResult)
{
    FontSampler sampler(&lv_font_montserrat_24, 12, 18);

    // Low threshold should include more (lighter) pixels.
    auto patternLow = sampler.sampleCharacter('0', 0.1f);

    // High threshold should include fewer (only brightest) pixels.
    auto patternHigh = sampler.sampleCharacter('0', 0.8f);

    int countLow = 0;
    int countHigh = 0;

    for (const auto& row : patternLow) {
        for (bool cell : row) {
            if (cell) countLow++;
        }
    }

    for (const auto& row : patternHigh) {
        for (bool cell : row) {
            if (cell) countHigh++;
        }
    }

    EXPECT_GT(countLow, countHigh) << "Lower threshold should capture more pixels";
}

TEST_F(FontSamplerTest, TrimPattern_RemovesEmptyBorder)
{
    // Create a pattern with empty border around filled center.
    std::vector<std::vector<bool>> pattern = {
        { false, false, false, false, false },
        { false, true, true, false, false },
        { false, true, true, false, false },
        { false, false, false, false, false },
    };

    auto trimmed = FontSampler::trimPattern(pattern);

    ASSERT_EQ(trimmed.size(), 2);
    ASSERT_EQ(trimmed[0].size(), 2);
    EXPECT_TRUE(trimmed[0][0]);
    EXPECT_TRUE(trimmed[0][1]);
    EXPECT_TRUE(trimmed[1][0]);
    EXPECT_TRUE(trimmed[1][1]);
}

TEST_F(FontSamplerTest, TrimPattern_EmptyPatternReturnsEmpty)
{
    std::vector<std::vector<bool>> pattern = {
        { false, false, false },
        { false, false, false },
    };

    auto trimmed = FontSampler::trimPattern(pattern);

    EXPECT_TRUE(trimmed.empty());
}

TEST_F(FontSamplerTest, TrimPattern_NoTrimNeeded)
{
    std::vector<std::vector<bool>> pattern = {
        { true, true },
        { true, true },
    };

    auto trimmed = FontSampler::trimPattern(pattern);

    ASSERT_EQ(trimmed.size(), 2);
    ASSERT_EQ(trimmed[0].size(), 2);
}

TEST_F(FontSamplerTest, HasClipping_DetectsTopEdge)
{
    std::vector<std::vector<bool>> pattern = {
        { false, true, false },
        { false, false, false },
        { false, false, false },
    };

    EXPECT_TRUE(FontSampler::hasClipping(pattern));
}

TEST_F(FontSamplerTest, HasClipping_DetectsBottomEdge)
{
    std::vector<std::vector<bool>> pattern = {
        { false, false, false },
        { false, false, false },
        { false, true, false },
    };

    EXPECT_TRUE(FontSampler::hasClipping(pattern));
}

TEST_F(FontSamplerTest, HasClipping_DetectsLeftEdge)
{
    std::vector<std::vector<bool>> pattern = {
        { false, false, false },
        { true, false, false },
        { false, false, false },
    };

    EXPECT_TRUE(FontSampler::hasClipping(pattern));
}

TEST_F(FontSamplerTest, HasClipping_DetectsRightEdge)
{
    std::vector<std::vector<bool>> pattern = {
        { false, false, false },
        { false, false, true },
        { false, false, false },
    };

    EXPECT_TRUE(FontSampler::hasClipping(pattern));
}

TEST_F(FontSamplerTest, HasClipping_NoClipping)
{
    std::vector<std::vector<bool>> pattern = {
        { false, false, false },
        { false, true, false },
        { false, false, false },
    };

    EXPECT_FALSE(FontSampler::hasClipping(pattern));
}

TEST_F(FontSamplerTest, SampleCharacterTrimmed_ReturnsTrimmedPattern)
{
    // Use a large canvas so there's definitely whitespace to trim.
    FontSampler sampler(&lv_font_montserrat_24, 50, 50);
    auto trimmed = sampler.sampleCharacterTrimmed('0');

    // Trimmed pattern should be smaller than original canvas.
    EXPECT_FALSE(trimmed.empty());
    EXPECT_LT(trimmed.size(), 50);
    EXPECT_LT(trimmed[0].size(), 50);

    // Trimmed pattern should have reasonable dimensions for a digit.
    // Montserrat 24pt digits are roughly 14x17 pixels.
    EXPECT_GT(trimmed.size(), 10);
    EXPECT_GT(trimmed[0].size(), 5);
}

TEST_F(FontSamplerTest, SampleCharacterTrimmed_AutoResizesOnClipping)
{
    // Use a small canvas that will clip (Montserrat 24pt needs ~14x17).
    // Canvas needs to be at least 5x5 to have any drawing area after 2px margins.
    FontSampler sampler(&lv_font_montserrat_24, 10, 10);
    auto trimmed = sampler.sampleCharacterTrimmed('0');

    // Should have auto-resized and still return a valid pattern.
    EXPECT_FALSE(trimmed.empty());

    // Canvas should have grown from the original 10x10.
    EXPECT_GT(sampler.getWidth(), 10);
    EXPECT_GT(sampler.getHeight(), 10);
}

TEST_F(FontSamplerTest, GetCachedPatternTrimmed_CachesResult)
{
    FontSampler sampler(&lv_font_montserrat_24, 50, 50);

    const auto& pattern1 = sampler.getCachedPatternTrimmed('A');
    const auto& pattern2 = sampler.getCachedPatternTrimmed('A');

    EXPECT_EQ(&pattern1, &pattern2) << "Cached trimmed pattern should return same reference";
}

TEST_F(FontSamplerTest, ResizeCanvas_ClearsCache)
{
    FontSampler sampler(&lv_font_montserrat_24, 50, 50);

    // Cache a pattern.
    const auto& pattern1 = sampler.getCachedPattern('A');
    (void)pattern1; // Suppress unused warning.

    // Resize canvas.
    sampler.resizeCanvas(60, 60);

    // New pattern should be different reference (cache was cleared).
    const auto& pattern2 = sampler.getCachedPattern('A');

    // Size should have changed.
    EXPECT_EQ(sampler.getWidth(), 60);
    EXPECT_EQ(sampler.getHeight(), 60);
    EXPECT_EQ(pattern2.size(), 60);
}

// Debug helper to print pattern (disabled by default).
TEST_F(FontSamplerTest, DISABLED_PrintPatternForVisualization)
{
    // Montserrat 24pt needs ~15x24 to fit digits fully.
    FontSampler sampler(&lv_font_montserrat_24, 15, 24);

    for (char c = '0'; c <= '9'; ++c) {
        auto pattern = sampler.sampleCharacter(c);
        spdlog::info("Pattern for '{}':", c);

        for (const auto& row : pattern) {
            std::string line;
            for (bool cell : row) {
                line += cell ? "█" : " ";
            }
            spdlog::info("  {}", line);
        }
        spdlog::info("");
    }
}

// Debug helper to print trimmed patterns (disabled by default).
TEST_F(FontSamplerTest, DISABLED_PrintTrimmedPatternForVisualization)
{
    // Use large canvas to ensure no clipping.
    FontSampler sampler(&lv_font_montserrat_24, 50, 50);

    for (char c = '0'; c <= '9'; ++c) {
        auto pattern = sampler.sampleCharacterTrimmed(c);
        spdlog::info("Trimmed pattern for '{}' ({}x{}):", c, pattern[0].size(), pattern.size());

        for (const auto& row : pattern) {
            std::string line;
            for (bool cell : row) {
                line += cell ? "█" : " ";
            }
            spdlog::info("  {}", line);
        }
        spdlog::info("");
    }
}

// Debug helper to check raw RGB values from NotoColorEmoji.
TEST_F(FontSamplerTest, DISABLED_NotoColorEmoji_RawRgb)
{
    // Load NotoColorEmoji font via FreeType.
    FontSampler sampler("fonts/NotoColorEmoji.ttf", 109, 120, 120, 0.3f);

    // Test both: plain digit "0" vs duck emoji.
    spdlog::info("=== Testing plain digit '0' ===");
    auto grid0 = sampler.sampleUtf8CharacterRgbGrid("0");
    std::map<uint32_t, int> colors0;
    for (int y = 0; y < grid0.height; ++y) {
        for (int x = 0; x < grid0.width; ++x) {
            const auto& px = grid0.at(x, y);
            uint32_t key = (px.r << 24) | (px.g << 16) | (px.b << 8) | px.a;
            colors0[key]++;
        }
    }
    spdlog::info("Digit '0': {} unique colors", colors0.size());

    spdlog::info("=== Testing duck emoji ===");
    auto grid = sampler.sampleUtf8CharacterRgbGrid("\xF0\x9F\xA6\x86"); // U+1F986 duck emoji

    spdlog::info("Raw RGB for duck emoji ({}x{}):", grid.width, grid.height);

    // Sample a few pixels to see what we're getting.
    for (int y = 0; y < std::min(5, static_cast<int>(grid.height)); ++y) {
        std::string line;
        for (int x = 0; x < std::min(10, static_cast<int>(grid.width)); ++x) {
            const auto& px = grid.at(x, y);
            line += fmt::format("({:3},{:3},{:3},{:3}) ", px.r, px.g, px.b, px.a);
        }
        spdlog::info("  Row {}: {}", y, line);
    }

    // Count unique colors.
    std::map<uint32_t, int> colorCounts;
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            const auto& px = grid.at(x, y);
            uint32_t key = (px.r << 24) | (px.g << 16) | (px.b << 8) | px.a;
            colorCounts[key]++;
        }
    }

    spdlog::info("Unique colors: {}", colorCounts.size());
    for (const auto& [color, count] : colorCounts) {
        uint8_t r = (color >> 24) & 0xFF;
        uint8_t g = (color >> 16) & 0xFF;
        uint8_t b = (color >> 8) & 0xFF;
        uint8_t a = color & 0xFF;
        spdlog::info("  RGBA({:3},{:3},{:3},{:3}): {} pixels", r, g, b, a, count);
    }
}

// Debug helper to test NotoColorEmoji material sampling.
TEST_F(FontSamplerTest, DISABLED_NotoColorEmoji_MaterialDistribution)
{
    // Load NotoColorEmoji font via FreeType.
    // NotoColorEmoji has fixed 109px bitmaps, so use a canvas large enough to fit.
    FontSampler sampler("fonts/NotoColorEmoji.ttf", 109, 120, 120, 0.3f);

    // Material abbreviations for visualization.
    auto materialChar = [](Material::EnumType m) -> char {
        switch (m) {
            case Material::EnumType::Air:
                return ' ';
            case Material::EnumType::Dirt:
                return 'D';
            case Material::EnumType::Leaf:
                return 'L';
            case Material::EnumType::Metal:
                return 'M';
            case Material::EnumType::Root:
                return 'R';
            case Material::EnumType::Sand:
                return 'S';
            case Material::EnumType::Seed:
                return 'E';
            case Material::EnumType::Wall:
                return 'W';
            case Material::EnumType::Water:
                return 'B'; // Blue.
            case Material::EnumType::Wood:
                return 'O'; // Oak/brown.
        }
        return '?';
    };

    // Sample digits 0-9.
    for (char c = '0'; c <= '9'; ++c) {
        std::string utf8(1, c);
        auto grid = sampler.sampleUtf8CharacterMaterialGrid(utf8);

        // Count material distribution.
        std::map<Material::EnumType, int> counts;
        for (int y = 0; y < grid.height; ++y) {
            for (int x = 0; x < grid.width; ++x) {
                counts[grid.at(x, y)]++;
            }
        }

        spdlog::info("Digit '{}' ({}x{}):", c, grid.width, grid.height);

        // Print distribution.
        for (const auto& [mat, count] : counts) {
            if (mat != Material::EnumType::Air && count > 0) {
                spdlog::info("  {}: {} pixels", toString(mat), count);
            }
        }

        // Print visual grid.
        for (int y = 0; y < grid.height; ++y) {
            std::string line;
            for (int x = 0; x < grid.width; ++x) {
                line += materialChar(grid.at(x, y));
            }
            spdlog::info("  |{}|", line);
        }
        spdlog::info("");
    }
}

// Test downsampling from native 109px to smaller sizes.
TEST_F(FontSamplerTest, DISABLED_DownsampleEmoji)
{
    // Load NotoColorEmoji at native 109px size.
    FontSampler sampler("fonts/NotoColorEmoji.ttf", 109, 120, 120, 0.3f);

    // Sample duck emoji at full resolution.
    auto fullGrid = sampler.sampleUtf8CharacterMaterialGrid("\xF0\x9F\xA6\x86", 0.5f);
    spdlog::info("Full resolution: {}x{}", fullGrid.width, fullGrid.height);

    // Downsample to various sizes.
    std::vector<int> sizes = { 36, 24, 16, 12 };

    auto materialChar = [](Material::EnumType m) -> char {
        switch (m) {
            case Material::EnumType::Air:
                return ' ';
            case Material::EnumType::Dirt:
                return 'D';
            case Material::EnumType::Leaf:
                return 'L';
            case Material::EnumType::Metal:
                return 'M';
            case Material::EnumType::Root:
                return 'R';
            case Material::EnumType::Sand:
                return 'S';
            case Material::EnumType::Seed:
                return 'E';
            case Material::EnumType::Wall:
                return 'W';
            case Material::EnumType::Water:
                return 'B';
            case Material::EnumType::Wood:
                return 'O';
        }
        return '?';
    };

    for (int size : sizes) {
        auto small = FontSampler::downsample(fullGrid, size, size);
        spdlog::info("\n=== Duck at {}x{} ===", size, size);

        // Count materials.
        std::map<Material::EnumType, int> counts;
        for (int y = 0; y < small.height; ++y) {
            for (int x = 0; x < small.width; ++x) {
                counts[small.at(x, y)]++;
            }
        }

        for (const auto& [mat, count] : counts) {
            if (mat != Material::EnumType::Air && count > 0) {
                spdlog::info("  {}: {} pixels", static_cast<int>(mat), count);
            }
        }

        // Print visual grid.
        for (int y = 0; y < small.height; ++y) {
            std::string line;
            for (int x = 0; x < small.width; ++x) {
                line += materialChar(small.at(x, y));
            }
            spdlog::info("  |{}|", line);
        }
    }
}

// Verify that bitmap font auto-detection expands canvas when needed.
TEST_F(FontSamplerTest, BitmapFontAutoDetection_ExpandsCanvasForNativeSize)
{
    // NotoColorEmoji has 109px native bitmaps.
    // Pass intentionally wrong params (32px font, 36x36 canvas).
    // Auto-detection should expand canvas to fit the native 109px glyphs.
    FontSampler sampler("fonts/NotoColorEmoji.ttf", 32, 36, 36, 0.3f);

    // Canvas should have been auto-expanded to at least 109+11=120px.
    EXPECT_GE(sampler.getWidth(), 109) << "Canvas width should be expanded for bitmap font";
    EXPECT_GE(sampler.getHeight(), 109) << "Canvas height should be expanded for bitmap font";

    // Verify we can actually sample an emoji without clipping.
    auto grid = sampler.sampleUtf8CharacterMaterialGrid("\xF0\x9F\xA6\x86", 0.5f); // Duck emoji.
    EXPECT_GT(grid.width, 0) << "Should be able to sample emoji";
    EXPECT_GT(grid.height, 0) << "Should be able to sample emoji";

    // Count non-AIR materials - should have substantial content.
    int nonAirCount = 0;
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            if (grid.at(x, y) != Material::EnumType::Air) {
                nonAirCount++;
            }
        }
    }
    EXPECT_GT(nonAirCount, 100) << "Emoji should have substantial non-AIR content";
}

} // namespace
} // namespace DirtSim
