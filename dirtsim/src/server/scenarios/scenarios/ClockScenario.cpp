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

    recalculateDimensions();
}

int ClockScenario::getDigitWidth() const
{
    switch (config_.font) {
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_WIDTH;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_WIDTH;
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_WIDTH;
    }
    return ClockFonts::SEGMENT7_WIDTH;
}

int ClockScenario::getDigitHeight() const
{
    switch (config_.font) {
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_HEIGHT;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_HEIGHT;
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_HEIGHT;
    }
    return ClockFonts::SEGMENT7_HEIGHT;
}

int ClockScenario::getDigitGap() const
{
    switch (config_.font) {
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_GAP;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_GAP;
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_GAP;
    }
    return ClockFonts::SEGMENT7_GAP;
}

int ClockScenario::getColonWidth() const
{
    switch (config_.font) {
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_COLON_WIDTH;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_COLON_WIDTH;
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_COLON_WIDTH;
    }
    return ClockFonts::SEGMENT7_COLON_WIDTH;
}

int ClockScenario::getColonPadding() const
{
    switch (config_.font) {
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_COLON_PADDING;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_COLON_PADDING;
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_COLON_PADDING;
    }
    return ClockFonts::SEGMENT7_COLON_PADDING;
}

void ClockScenario::recalculateDimensions()
{
    int clock_width = calculateTotalWidth();
    int clock_height = getDigitHeight();

    // Buffer cells around clock edges.
    constexpr int BUFFER = 4;

    // Auto-scale mode: size world to match display aspect ratio.
    if (config_.auto_scale && config_.target_display_width > 0 && config_.target_display_height > 0) {
        // Use FULL display aspect (what CellRenderer uses to fill the screen).
        // Margins just provide minimum buffer around clock, not affect aspect.
        double display_aspect = static_cast<double>(config_.target_display_width)
            / config_.target_display_height;

        // Base world size: clock + buffer.
        int base_width = clock_width + 2 * BUFFER;
        int base_height = clock_height + 2 * BUFFER;
        double clock_aspect = static_cast<double>(base_width) / base_height;

        // Adjust world to match display aspect ratio.
        // This ensures CellRenderer fills the display without gray bands.
        int world_width, world_height;
        if (display_aspect > clock_aspect) {
            // Display is wider than clock - expand width.
            world_height = base_height;
            world_width = static_cast<int>(std::ceil(world_height * display_aspect));
        }
        else {
            // Display is taller than clock - expand height.
            world_width = base_width;
            world_height = static_cast<int>(std::ceil(world_width / display_aspect));
        }

        // Use scale=1 (each font pixel = 1 cell).
        config_.horizontal_scale = 1.0;
        config_.vertical_scale = 1.0;

        metadata_.requiredWidth = static_cast<uint32_t>(world_width);
        metadata_.requiredHeight = static_cast<uint32_t>(world_height);

        spdlog::info(
            "ClockScenario: Auto-scale - display={}x{}, clock={}x{}, world={}x{} (aspect matched)",
            config_.target_display_width,
            config_.target_display_height,
            clock_width,
            clock_height,
            world_width,
            world_height);
    }
    else {
        // Manual scale mode (original behavior).
        metadata_.requiredWidth =
            static_cast<uint32_t>(std::ceil(clock_width * config_.horizontal_scale));
        metadata_.requiredHeight =
            static_cast<uint32_t>(std::ceil(clock_height * config_.vertical_scale));

        spdlog::info(
            "ClockScenario: Manual scale - clock={}x{}, scale=({:.2f}, {:.2f}), world={}x{}",
            clock_width,
            clock_height,
            config_.horizontal_scale,
            config_.vertical_scale,
            metadata_.requiredWidth,
            metadata_.requiredHeight);
    }
}

const ScenarioMetadata& ClockScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig ClockScenario::getConfig() const
{
    return config_;
}

void ClockScenario::setConfig(const ScenarioConfig& newConfig, World& world)
{
    if (std::holds_alternative<ClockConfig>(newConfig)) {
        const ClockConfig& incoming = std::get<ClockConfig>(newConfig);

        // Check if any dimension-affecting settings changed.
        bool needs_resize = (incoming.show_seconds != config_.show_seconds) ||
                            (incoming.font != config_.font) ||
                            (incoming.auto_scale != config_.auto_scale) ||
                            (incoming.target_display_width != config_.target_display_width) ||
                            (incoming.target_display_height != config_.target_display_height) ||
                            (incoming.margin_pixels != config_.margin_pixels);

        config_ = incoming;

        // Recalculate and resize if dimensions changed.
        if (needs_resize) {
            recalculateDimensions();

            spdlog::info(
                "ClockScenario: Resizing world to {}x{} (font={}, show_seconds={}, display={}x{})",
                metadata_.requiredWidth,
                metadata_.requiredHeight,
                static_cast<int>(config_.font),
                config_.show_seconds,
                config_.target_display_width,
                config_.target_display_height);

            world.resizeGrid(metadata_.requiredWidth, metadata_.requiredHeight);
        }

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
    int dw = getDigitWidth();
    int dg = getDigitGap();
    int cw = getColonWidth();
    int cp = getColonPadding();

    if (config_.show_seconds) {
        // HH : MM : SS (6 digits, 2 colons).
        // Layout: D gap D pad colon pad D gap D pad colon pad D gap D.
        return 6 * dw + 4 * dg + 2 * (cw + 2 * cp);
    }
    else {
        // HH : MM (4 digits, 1 colon).
        // Layout: D gap D pad colon pad D gap D.
        return 4 * dw + 2 * dg + (cw + 2 * cp);
    }
}

void ClockScenario::drawDigit(World& world, int digit, int start_x, int start_y)
{
    if (digit < 0 || digit > 9) {
        return;
    }

    int dw = getDigitWidth();
    int dh = getDigitHeight();

    for (int row = 0; row < dh; ++row) {
        for (int col = 0; col < dw; ++col) {
            int x = start_x + col;
            int y = start_y + row;

            // Bounds check.
            if (x < 0 || x >= static_cast<int>(world.getData().width) || y < 0 ||
                y >= static_cast<int>(world.getData().height)) {
                continue;
            }

            // Get the pixel value from the appropriate pattern.
            bool pixel = false;
            switch (config_.font) {
            case ClockFont::Segment7:
                pixel = ClockFonts::SEGMENT7_PATTERNS[digit][row][col];
                break;
            case ClockFont::Segment7Large:
                pixel = ClockFonts::SEGMENT7_LARGE_PATTERNS[digit][row][col];
                break;
            case ClockFont::DotMatrix:
                pixel = ClockFonts::DOT_MATRIX_PATTERNS[digit][row][col];
                break;
            }

            if (pixel) {
                world.getData().at(x, y).replaceMaterial(MaterialType::WALL, 1.0);
            }
        }
    }
}

void ClockScenario::drawColon(World& world, int start_x, int start_y)
{
    int dh = getDigitHeight();
    int cw = getColonWidth();

    // Calculate dot positions at roughly 1/3 and 2/3 of digit height.
    int dot1_y = start_y + dh / 3;
    int dot2_y = start_y + (2 * dh) / 3;

    // Draw colon dots (size depends on font).
    for (int dx = 0; dx < cw; ++dx) {
        int x = start_x + dx;
        if (x < 0 || x >= static_cast<int>(world.getData().width)) {
            continue;
        }

        // For large font, draw 2x2 dots; otherwise single pixels.
        int dot_height = (config_.font == ClockFont::Segment7Large) ? 2 : 1;

        for (int dy = 0; dy < dot_height; ++dy) {
            int y1 = dot1_y + dy;
            int y2 = dot2_y + dy;

            if (y1 >= 0 && y1 < static_cast<int>(world.getData().height)) {
                world.getData().at(x, y1).replaceMaterial(MaterialType::WALL, 1.0);
            }
            if (y2 >= 0 && y2 < static_cast<int>(world.getData().height)) {
                world.getData().at(x, y2).replaceMaterial(MaterialType::WALL, 1.0);
            }
        }
    }
}

void ClockScenario::drawTime(World& world)
{
    // Get current time.
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm* time_info;
    if (config_.timezone_index == 0) {
        // Local system time.
        time_info = std::localtime(&now_time);
    }
    else {
        // UTC time with offset.
        time_info = std::gmtime(&now_time);
        const auto& tz = TIMEZONES[config_.timezone_index];

        // Apply timezone offset (hours).
        time_info->tm_hour += tz.offset_hours;

        // Normalize hours (handle wraparound).
        if (time_info->tm_hour < 0) {
            time_info->tm_hour += 24;
        }
        else if (time_info->tm_hour >= 24) {
            time_info->tm_hour -= 24;
        }
    }

    int hours = time_info->tm_hour;
    int minutes = time_info->tm_min;
    int seconds = time_info->tm_sec;

    // Clear world first.
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }

    // Get font dimensions.
    int dw = getDigitWidth();
    int dh = getDigitHeight();
    int dg = getDigitGap();
    int cw = getColonWidth();
    int cp = getColonPadding();

    // Calculate centered position.
    int total_width = calculateTotalWidth();
    int start_x = (static_cast<int>(world.getData().width) - total_width) / 2;
    int start_y = (static_cast<int>(world.getData().height) - dh) / 2;

    int cursor_x = start_x;

    // Draw hours (tens, ones).
    drawDigit(world, hours / 10, cursor_x, start_y);
    cursor_x += dw + dg;
    drawDigit(world, hours % 10, cursor_x, start_y);
    cursor_x += dw;

    // Draw first colon.
    cursor_x += cp;
    drawColon(world, cursor_x, start_y);
    cursor_x += cw + cp;

    // Draw minutes (tens, ones).
    drawDigit(world, minutes / 10, cursor_x, start_y);
    cursor_x += dw + dg;
    drawDigit(world, minutes % 10, cursor_x, start_y);
    cursor_x += dw;

    // Draw seconds if enabled.
    if (config_.show_seconds) {
        // Draw second colon.
        cursor_x += cp;
        drawColon(world, cursor_x, start_y);
        cursor_x += cw + cp;

        // Draw seconds (tens, ones).
        drawDigit(world, seconds / 10, cursor_x, start_y);
        cursor_x += dw + dg;
        drawDigit(world, seconds % 10, cursor_x, start_y);
    }
}

} // namespace DirtSim
