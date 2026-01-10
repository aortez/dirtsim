#include "core/FontSampler.h"

#include <gtest/gtest.h>
#include <lvgl.h>
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

} // namespace
} // namespace DirtSim
