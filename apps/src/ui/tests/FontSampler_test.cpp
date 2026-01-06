#include "ui/rendering/FontSampler.h"

#include <gtest/gtest.h>
#include <lvgl.h>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {
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

} // namespace
} // namespace Ui
} // namespace DirtSim
