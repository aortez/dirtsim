#include "ClockScenario.h"
#include "core/Cell.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cmath>
#include <ctime>

namespace DirtSim {

ClockScenario::ClockScenario()
{
    metadata_.name = "Clock";
    metadata_.description = "Digital clock displaying system time (HH:MM:SS)";
    metadata_.category = "demo";

    // Compute required world dimensions from clock size and scale factors.
    int clock_width = calculateTotalWidth();
    int clock_height = DIGIT_HEIGHT;

    metadata_.requiredWidth =
        static_cast<uint32_t>(std::ceil(clock_width * config_.horizontal_scale));
    metadata_.requiredHeight =
        static_cast<uint32_t>(std::ceil(clock_height * config_.vertical_scale));

    spdlog::info(
        "ClockScenario: clock={}x{}, scale=({:.2f}, {:.2f}), world={}x{}",
        clock_width,
        clock_height,
        config_.horizontal_scale,
        config_.vertical_scale,
        metadata_.requiredWidth,
        metadata_.requiredHeight);
}

const ScenarioMetadata& ClockScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig ClockScenario::getConfig() const
{
    return config_;
}

void ClockScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    if (std::holds_alternative<ClockConfig>(newConfig)) {
        config_ = std::get<ClockConfig>(newConfig);
        spdlog::info("ClockScenario: Config updated");
    }
    else {
        spdlog::error("ClockScenario: Invalid config type provided");
    }
}

void ClockScenario::setup(World& world)
{
    spdlog::info("ClockScenario::setup - initializing clock display");

    // Clear world to empty state.
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }

    // Draw current time.
    drawTime(world);

    spdlog::info("ClockScenario::setup complete");
}

void ClockScenario::reset(World& world)
{
    spdlog::info("ClockScenario::reset");
    setup(world);
}

void ClockScenario::tick(World& world, double /*deltaTime*/)
{
    // Get current time.
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);

    int current_second = local_time->tm_sec;

    // Only redraw if the second has changed.
    if (current_second != last_second_) {
        last_second_ = current_second;
        drawTime(world);
    }
}

int ClockScenario::calculateTotalWidth() const
{
    // 6 digits + 5 gaps between digits + 2 colons with padding.
    // HH : MM : SS
    // D D c D D c D D  (D = digit, c = colon with padding).
    // Layout: D gap D pad colon pad D gap D pad colon pad D gap D.
    // = 6 * DIGIT_WIDTH + 4 * DIGIT_GAP + 2 * (COLON_WIDTH + 2 * COLON_PADDING).
    return 6 * DIGIT_WIDTH + 4 * DIGIT_GAP + 2 * (COLON_WIDTH + 2 * COLON_PADDING);
}

void ClockScenario::drawDigit(World& world, int digit, int start_x, int start_y)
{
    if (digit < 0 || digit > 9) {
        return;
    }

    const auto& pattern = DIGIT_PATTERNS[digit];
    for (int row = 0; row < DIGIT_HEIGHT; ++row) {
        for (int col = 0; col < DIGIT_WIDTH; ++col) {
            int x = start_x + col;
            int y = start_y + row;

            // Bounds check.
            if (x < 0 || x >= static_cast<int>(world.getData().width) || y < 0 ||
                y >= static_cast<int>(world.getData().height)) {
                continue;
            }

            if (pattern[row][col]) {
                world.getData().at(x, y).replaceMaterial(MaterialType::WALL, 1.0);
            }
        }
    }
}

void ClockScenario::drawColon(World& world, int x, int start_y)
{
    // Bounds check.
    if (x < 0 || x >= static_cast<int>(world.getData().width)) {
        return;
    }

    // Draw two dots at 1/3 and 2/3 of the digit height.
    int dot1_y = start_y + 2;
    int dot2_y = start_y + 4;

    if (dot1_y >= 0 && dot1_y < static_cast<int>(world.getData().height)) {
        world.getData().at(x, dot1_y).replaceMaterial(MaterialType::WALL, 1.0);
    }
    if (dot2_y >= 0 && dot2_y < static_cast<int>(world.getData().height)) {
        world.getData().at(x, dot2_y).replaceMaterial(MaterialType::WALL, 1.0);
    }
}

void ClockScenario::drawTime(World& world)
{
    // Get current time.
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);

    int hours = local_time->tm_hour;
    int minutes = local_time->tm_min;
    int seconds = local_time->tm_sec;

    // Clear world first.
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }

    // Calculate centered position.
    int total_width = calculateTotalWidth();
    int start_x = (static_cast<int>(world.getData().width) - total_width) / 2;
    int start_y = (static_cast<int>(world.getData().height) - DIGIT_HEIGHT) / 2;

    int cursor_x = start_x;

    // Draw hours (tens, ones).
    drawDigit(world, hours / 10, cursor_x, start_y);
    cursor_x += DIGIT_WIDTH + DIGIT_GAP;
    drawDigit(world, hours % 10, cursor_x, start_y);
    cursor_x += DIGIT_WIDTH;

    // Draw first colon.
    cursor_x += COLON_PADDING;
    drawColon(world, cursor_x, start_y);
    cursor_x += COLON_WIDTH + COLON_PADDING;

    // Draw minutes (tens, ones).
    drawDigit(world, minutes / 10, cursor_x, start_y);
    cursor_x += DIGIT_WIDTH + DIGIT_GAP;
    drawDigit(world, minutes % 10, cursor_x, start_y);
    cursor_x += DIGIT_WIDTH;

    // Draw second colon.
    cursor_x += COLON_PADDING;
    drawColon(world, cursor_x, start_y);
    cursor_x += COLON_WIDTH + COLON_PADDING;

    // Draw seconds (tens, ones).
    drawDigit(world, seconds / 10, cursor_x, start_y);
    cursor_x += DIGIT_WIDTH + DIGIT_GAP;
    drawDigit(world, seconds % 10, cursor_x, start_y);
}

} // namespace DirtSim
