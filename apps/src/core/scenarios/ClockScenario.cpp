#include "ClockScenario.h"
#include "core/Cell.h"
#include "core/MaterialMove.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldCollisionCalculator.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/OrganismManager.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cmath>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DirtSim {

// ============================================================================
// Default Event Configurations
// ============================================================================

const std::map<ClockEventType, EventTypeConfig> ClockScenario::DEFAULT_EVENT_CONFIGS = {
    { ClockEventType::DUCK, { .duration = 30.0, .chance_per_second = 0.05, .cooldown = 10.0 } },
    { ClockEventType::MELTDOWN, { .duration = 20.0, .chance_per_second = 0.02, .cooldown = 30.0 } },
    { ClockEventType::RAIN, { .duration = 20.0, .chance_per_second = 0.05, .cooldown = 10.0 } },
};

// ============================================================================
// DoorManager Implementation
// ============================================================================

Vector2i DoorManager::calculateRoofPos(Vector2i door_pos, DoorSide side)
{
    // Roof goes up one and inward one.
    // Left door: inward = +1 x. Right door: inward = -1 x.
    int dx = (side == DoorSide::LEFT) ? 1 : -1;
    return Vector2i{ door_pos.x + dx, door_pos.y - 1 };
}

bool DoorManager::openDoor(Vector2i pos, DoorSide side, World& world)
{
    // Check if already open.
    if (doors_.contains(pos) && doors_[pos].is_open) {
        return false;
    }

    DoorState state;
    state.is_open = true;
    state.side = side;
    state.door_pos = pos;
    state.roof_pos = calculateRoofPos(pos, side);

    // Clear the door cell (make it passable).
    world.getData().at(pos.x, pos.y) = Cell();

    // Place wall at roof position (displace any organisms).
    world.replaceMaterialAtCell(state.roof_pos.x, state.roof_pos.y, MaterialType::WALL);

    doors_[pos] = state;

    spdlog::info("DoorManager: Opened door at ({}, {}), roof at ({}, {})",
        pos.x, pos.y, state.roof_pos.x, state.roof_pos.y);

    return true;
}

void DoorManager::closeDoor(Vector2i pos, World& world)
{
    auto it = doors_.find(pos);
    if (it == doors_.end() || !it->second.is_open) {
        return;
    }

    const DoorState& state = it->second;

    // Restore wall at door position (push any organisms out of the way).
    world.replaceMaterialAtCell(pos.x, pos.y, MaterialType::WALL);

    // Clear roof cell (it will be restored by normal wall drawing if needed).
    Cell& roof_cell = world.getData().at(state.roof_pos.x, state.roof_pos.y);
    roof_cell = Cell();

    spdlog::info("DoorManager: Closed door at ({}, {})", pos.x, pos.y);

    doors_.erase(it);
}

bool DoorManager::isOpenDoor(Vector2i pos) const
{
    auto it = doors_.find(pos);
    return it != doors_.end() && it->second.is_open;
}

bool DoorManager::isRoofCell(Vector2i pos) const
{
    for (const auto& [door_pos, state] : doors_) {
        if (state.is_open && state.roof_pos == pos) {
            return true;
        }
    }
    return false;
}

std::vector<Vector2i> DoorManager::getOpenDoorPositions() const
{
    std::vector<Vector2i> positions;
    for (const auto& [pos, state] : doors_) {
        if (state.is_open) {
            positions.push_back(pos);
        }
    }
    return positions;
}

std::vector<Vector2i> DoorManager::getRoofPositions() const
{
    std::vector<Vector2i> positions;
    for (const auto& [pos, state] : doors_) {
        if (state.is_open) {
            positions.push_back(state.roof_pos);
        }
    }
    return positions;
}

void DoorManager::closeAllDoors(World& world)
{
    // Collect positions first to avoid iterator invalidation.
    std::vector<Vector2i> positions;
    for (const auto& [pos, state] : doors_) {
        if (state.is_open) {
            positions.push_back(pos);
        }
    }
    for (const auto& pos : positions) {
        closeDoor(pos, world);
    }
}

// ============================================================================
// ClockScenario Implementation
// ============================================================================

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
    case Config::ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_WIDTH;
    case Config::ClockFont::Segment7:
        return ClockFonts::SEGMENT7_WIDTH;
    case Config::ClockFont::Segment7ExtraTall:
        return ClockFonts::SEGMENT7_EXTRA_TALL_WIDTH;
    case Config::ClockFont::Segment7Jumbo:
        return ClockFonts::SEGMENT7_JUMBO_WIDTH;
    case Config::ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_WIDTH;
    case Config::ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_WIDTH;
    }
    return ClockFonts::SEGMENT7_WIDTH;
}

int ClockScenario::getDigitHeight() const
{
    switch (config_.font) {
    case Config::ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_HEIGHT;
    case Config::ClockFont::Segment7:
        return ClockFonts::SEGMENT7_HEIGHT;
    case Config::ClockFont::Segment7ExtraTall:
        return ClockFonts::SEGMENT7_EXTRA_TALL_HEIGHT;
    case Config::ClockFont::Segment7Jumbo:
        return ClockFonts::SEGMENT7_JUMBO_HEIGHT;
    case Config::ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_HEIGHT;
    case Config::ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_HEIGHT;
    }
    return ClockFonts::SEGMENT7_HEIGHT;
}

int ClockScenario::getDigitGap() const
{
    switch (config_.font) {
    case Config::ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_GAP;
    case Config::ClockFont::Segment7:
        return ClockFonts::SEGMENT7_GAP;
    case Config::ClockFont::Segment7ExtraTall:
        return ClockFonts::SEGMENT7_EXTRA_TALL_GAP;
    case Config::ClockFont::Segment7Jumbo:
        return ClockFonts::SEGMENT7_JUMBO_GAP;
    case Config::ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_GAP;
    case Config::ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_GAP;
    }
    return ClockFonts::SEGMENT7_GAP;
}

int ClockScenario::getColonWidth() const
{
    switch (config_.font) {
    case Config::ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_COLON_WIDTH;
    case Config::ClockFont::Segment7:
        return ClockFonts::SEGMENT7_COLON_WIDTH;
    case Config::ClockFont::Segment7ExtraTall:
        return ClockFonts::SEGMENT7_EXTRA_TALL_COLON_WIDTH;
    case Config::ClockFont::Segment7Jumbo:
        return ClockFonts::SEGMENT7_JUMBO_COLON_WIDTH;
    case Config::ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_COLON_WIDTH;
    case Config::ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_COLON_WIDTH;
    }
    return ClockFonts::SEGMENT7_COLON_WIDTH;
}

int ClockScenario::getColonPadding() const
{
    switch (config_.font) {
    case Config::ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_COLON_PADDING;
    case Config::ClockFont::Segment7:
        return ClockFonts::SEGMENT7_COLON_PADDING;
    case Config::ClockFont::Segment7ExtraTall:
        return ClockFonts::SEGMENT7_EXTRA_TALL_COLON_PADDING;
    case Config::ClockFont::Segment7Jumbo:
        return ClockFonts::SEGMENT7_JUMBO_COLON_PADDING;
    case Config::ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_COLON_PADDING;
    case Config::ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_COLON_PADDING;
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
    if (config_.autoScale && config_.targetDisplayWidth > 0 && config_.targetDisplayHeight > 0) {
        // Use FULL display aspect (what CellRenderer uses to fill the screen).
        // Margins just provide minimum buffer around clock, not affect aspect.
        double display_aspect = static_cast<double>(config_.targetDisplayWidth)
            / config_.targetDisplayHeight;

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
        config_.horizontalScale = 1.0;
        config_.verticalScale = 1.0;

        metadata_.requiredWidth = static_cast<uint32_t>(world_width);
        metadata_.requiredHeight = static_cast<uint32_t>(world_height);

        spdlog::info(
            "ClockScenario: Auto-scale - display={}x{}, clock={}x{}, world={}x{} (aspect matched)",
            config_.targetDisplayWidth,
            config_.targetDisplayHeight,
            clock_width,
            clock_height,
            world_width,
            world_height);
    }
    else {
        // Manual scale mode (original behavior).
        metadata_.requiredWidth =
            static_cast<uint32_t>(std::ceil(clock_width * config_.horizontalScale));
        metadata_.requiredHeight =
            static_cast<uint32_t>(std::ceil(clock_height * config_.verticalScale));

        spdlog::info(
            "ClockScenario: Manual scale - clock={}x{}, scale=({:.2f}, {:.2f}), world={}x{}",
            clock_width,
            clock_height,
            config_.horizontalScale,
            config_.verticalScale,
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
    if (std::holds_alternative<Config::Clock>(newConfig)) {
        const Config::Clock& incoming = std::get<Config::Clock>(newConfig);

        // Check if any dimension-affecting settings changed.
        bool needs_resize = (incoming.showSeconds != config_.showSeconds) ||
                            (incoming.font != config_.font) ||
                            (incoming.autoScale != config_.autoScale) ||
                            (incoming.targetDisplayWidth != config_.targetDisplayWidth) ||
                            (incoming.targetDisplayHeight != config_.targetDisplayHeight) ||
                            (incoming.marginPixels != config_.marginPixels);

        // Track event toggle changes before updating config_.
        bool rain_was_enabled = config_.rainEnabled;
        bool duck_was_enabled = config_.duckEnabled;

        config_ = incoming;

        // Recalculate and reset if dimensions changed (including font).
        if (needs_resize) {
            recalculateDimensions();

            spdlog::info(
                "ClockScenario: Resetting world to {}x{} (font={}, showSeconds={}, display={}x{})",
                metadata_.requiredWidth,
                metadata_.requiredHeight,
                static_cast<int>(config_.font),
                config_.showSeconds,
                config_.targetDisplayWidth,
                config_.targetDisplayHeight);

            // Cancel any active events before resizing.
            cancelAllEvents(world);

            world.resizeGrid(metadata_.requiredWidth, metadata_.requiredHeight);
            reset(world);  // Clear and redraw everything.
        }

        // Handle manual event toggling.
        // Rain event toggle.
        if (config_.rainEnabled && !rain_was_enabled) {
            // User enabled rain - start it if not already active.
            if (!active_events_.contains(ClockEventType::RAIN)) {
                event_cooldowns_[ClockEventType::RAIN] = 0.0;  // Clear cooldown.
                startEvent(world, ClockEventType::RAIN);
                spdlog::info("ClockScenario: Rain manually enabled");
            }
        }
        else if (!config_.rainEnabled && rain_was_enabled) {
            // User disabled rain - stop it if active.
            auto it = active_events_.find(ClockEventType::RAIN);
            if (it != active_events_.end()) {
                endEvent(world, ClockEventType::RAIN, it->second);
                active_events_.erase(it);
                event_cooldowns_[ClockEventType::RAIN] = 0.0;  // No cooldown for manual stop.
                spdlog::info("ClockScenario: Rain manually disabled");
            }
        }

        // Duck event toggle.
        if (config_.duckEnabled && !duck_was_enabled) {
            // User enabled duck - start it if not already active.
            if (!active_events_.contains(ClockEventType::DUCK)) {
                event_cooldowns_[ClockEventType::DUCK] = 0.0;  // Clear cooldown.
                startEvent(world, ClockEventType::DUCK);
                spdlog::info("ClockScenario: Duck manually enabled");
            }
        }
        else if (!config_.duckEnabled && duck_was_enabled) {
            // User disabled duck - stop it if active.
            auto it = active_events_.find(ClockEventType::DUCK);
            if (it != active_events_.end()) {
                endEvent(world, ClockEventType::DUCK, it->second);
                active_events_.erase(it);
                event_cooldowns_[ClockEventType::DUCK] = 0.0;  // No cooldown for manual stop.
                spdlog::info("ClockScenario: Duck manually disabled");
            }
        }

        // Meltdown event trigger (one-shot).
        if (config_.meltdownEnabled) {
            // User triggered meltdown - start it if not already active.
            if (!active_events_.contains(ClockEventType::MELTDOWN)) {
                event_cooldowns_[ClockEventType::MELTDOWN] = 0.0;  // Clear cooldown.
                startEvent(world, ClockEventType::MELTDOWN);
                spdlog::info("ClockScenario: Meltdown manually triggered");
            }
            // Reset the trigger flag immediately.
            config_.meltdownEnabled = false;
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

    // Add wall border around the world (for duck to run on).
    uint32_t width = world.getData().width;
    uint32_t height = world.getData().height;

    // Top and bottom borders.
    for (uint32_t x = 0; x < width; ++x) {
        world.replaceMaterialAtCell(x, 0, MaterialType::WALL);
        world.replaceMaterialAtCell(x, height - 1, MaterialType::WALL);
    }

    // Left and right borders.
    for (uint32_t y = 0; y < height; ++y) {
        world.replaceMaterialAtCell(0, y, MaterialType::WALL);
        world.replaceMaterialAtCell(width - 1, y, MaterialType::WALL);
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

void ClockScenario::tick(World& world, double deltaTime)
{
    // Redraw walls every frame (respects door state).
    redrawWalls(world);

    // Redraw digits every frame to keep METAL in place (unless meltdown is active).
    if (!isMeltdownActive()) {
        drawTime(world);
    }

    // Manage floor drain based on water level.
    updateDrain(world);

    // Update event system.
    updateEvents(world, deltaTime);
}

int ClockScenario::calculateTotalWidth() const
{
    int dw = getDigitWidth();
    int dg = getDigitGap();
    int cw = getColonWidth();
    int cp = getColonPadding();

    if (config_.showSeconds) {
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
            case Config::ClockFont::DotMatrix:
                pixel = ClockFonts::DOT_MATRIX_PATTERNS[digit][row][col];
                break;
            case Config::ClockFont::Segment7:
                pixel = ClockFonts::SEGMENT7_PATTERNS[digit][row][col];
                break;
            case Config::ClockFont::Segment7ExtraTall:
                pixel = ClockFonts::SEGMENT7_EXTRA_TALL_PATTERNS[digit][row][col];
                break;
            case Config::ClockFont::Segment7Jumbo:
                pixel = ClockFonts::SEGMENT7_JUMBO_PATTERNS[digit][row][col];
                break;
            case Config::ClockFont::Segment7Large:
                pixel = ClockFonts::SEGMENT7_LARGE_PATTERNS[digit][row][col];
                break;
            case Config::ClockFont::Segment7Tall:
                pixel = ClockFonts::SEGMENT7_TALL_PATTERNS[digit][row][col];
                break;
            }

            if (pixel) {
                world.replaceMaterialAtCell(x, y, MaterialType::METAL);
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
        int dot_height = (config_.font == Config::ClockFont::Segment7Large) ? 2 : 1;

        for (int dy = 0; dy < dot_height; ++dy) {
            int y1 = dot1_y + dy;
            int y2 = dot2_y + dy;

            if (y1 >= 0 && y1 < static_cast<int>(world.getData().height)) {
                world.replaceMaterialAtCell(x, y1, MaterialType::METAL);
            }
            if (y2 >= 0 && y2 < static_cast<int>(world.getData().height)) {
                world.replaceMaterialAtCell(x, y2, MaterialType::METAL);
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
    if (config_.timezoneIndex == 0) {
        // Local system time.
        time_info = std::localtime(&now_time);
    }
    else {
        // UTC time with offset.
        time_info = std::gmtime(&now_time);
        const auto& tz = TIMEZONES[config_.timezoneIndex];

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

    // Clear all metal from the world before drawing fresh digits.
    WorldData& data = world.getData();
    for (uint32_t y = 1; y < data.height - 1; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            if (data.at(x, y).material_type == MaterialType::METAL) {
                data.at(x, y) = Cell();
            }
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
    if (config_.showSeconds) {
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

// ============================================================================
// Event System
// ============================================================================

void ClockScenario::updateEvents(World& world, double deltaTime)
{
    // Events disabled if frequency is 0.
    if (config_.eventFrequency <= 0.0) {
        return;
    }

    // Decrement cooldowns.
    for (auto& [type, cooldown] : event_cooldowns_) {
        if (cooldown > 0.0) {
            cooldown -= deltaTime;
        }
    }

    // Check for new events once per second.
    time_since_last_trigger_check_ += deltaTime;
    if (time_since_last_trigger_check_ >= 1.0) {
        time_since_last_trigger_check_ = 0.0;
        tryTriggerEvents(world);
    }

    // Update active events and collect ones that need to end.
    std::vector<ClockEventType> events_to_end;

    for (auto& [type, event] : active_events_) {
        updateEvent(world, type, event, deltaTime);

        event.remaining_time -= deltaTime;
        if (event.remaining_time <= 0.0) {
            events_to_end.push_back(type);
        }
    }

    // End expired events.
    for (auto type : events_to_end) {
        auto it = active_events_.find(type);
        if (it != active_events_.end()) {
            endEvent(world, type, it->second);
            active_events_.erase(it);
        }
    }
}

void ClockScenario::tryTriggerEvents(World& world)
{
    // Check each event type for triggering.
    for (const auto& [type, config] : DEFAULT_EVENT_CONFIGS) {
        // Skip if already active.
        if (active_events_.contains(type)) {
            continue;
        }

        // Skip if on cooldown.
        if (event_cooldowns_.contains(type) && event_cooldowns_[type] > 0.0) {
            continue;
        }

        // Scale chance by eventFrequency config.
        double effective_chance = config.chance_per_second * config_.eventFrequency;

        if (uniform_dist_(rng_) < effective_chance) {
            startEvent(world, type);
        }
    }
}

void ClockScenario::startEvent(World& world, ClockEventType type)
{
    const auto& eventConfig = DEFAULT_EVENT_CONFIGS.at(type);

    ActiveEvent event;
    event.remaining_time = eventConfig.duration;

    if (type == ClockEventType::MELTDOWN) {
        MeltdownEventState melt_state;

        // Scan world to find the bottom edge of the digits (max Y with METAL).
        const WorldData& data = world.getData();
        int max_metal_y = 0;
        for (uint32_t y = 0; y < data.height; ++y) {
            for (uint32_t x = 0; x < data.width; ++x) {
                if (data.at(x, y).material_type == MaterialType::METAL) {
                    max_metal_y = std::max(max_metal_y, static_cast<int>(y));
                }
            }
        }
        melt_state.digit_bottom_y = max_metal_y;

        event.state = melt_state;
        spdlog::info("ClockScenario: Starting MELTDOWN event (duration: {}s, digit_bottom_y: {})",
            eventConfig.duration, melt_state.digit_bottom_y);
    }
    else if (type == ClockEventType::RAIN) {
        event.state = RainEventState{};
        config_.rainEnabled = true;  // Sync config flag.
        spdlog::info("ClockScenario: Starting RAIN event (duration: {}s)", eventConfig.duration);
    }
    else if (type == ClockEventType::DUCK) {
        DuckEventState duck_state;

        // Choose random entrance side and calculate door position.
        duck_state.entrance_side = (uniform_dist_(rng_) < 0.5) ? DoorSide::LEFT : DoorSide::RIGHT;
        uint32_t door_y = world.getData().height - 2;
        uint32_t door_x = (duck_state.entrance_side == DoorSide::LEFT) ? 0 : world.getData().width - 1;

        duck_state.entrance_door_pos = Vector2i{ static_cast<int>(door_x), static_cast<int>(door_y) };
        duck_state.entrance_door_open = true;
        duck_state.exit_door_open = false;

        // Calculate exit door position (opposite side).
        uint32_t exit_x = (duck_state.entrance_side == DoorSide::LEFT) ? world.getData().width - 1 : 0;
        duck_state.exit_door_pos = Vector2i{ static_cast<int>(exit_x), static_cast<int>(door_y) };

        // Open entrance door via DoorManager. Duck spawns after delay.
        door_manager_.openDoor(duck_state.entrance_door_pos, duck_state.entrance_side, world);
        duck_state.phase = DuckEventPhase::DOOR_OPENING;
        duck_state.door_open_timer = 0.0;

        spdlog::info("ClockScenario: Opening {} door for duck entrance",
            duck_state.entrance_side == DoorSide::LEFT ? "LEFT" : "RIGHT");

        event.state = duck_state;
        config_.duckEnabled = true;  // Sync config flag.
        spdlog::info("ClockScenario: Starting DUCK event (duration: {}s)", eventConfig.duration);
    }

    active_events_[type] = std::move(event);
}

void ClockScenario::updateEvent(World& world, ClockEventType /*type*/, ActiveEvent& event, double deltaTime)
{
    std::visit([&](auto& state) {
        using T = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<T, DuckEventState>) {
            updateDuckEvent(world, state, event.remaining_time, deltaTime);
        } else if constexpr (std::is_same_v<T, MeltdownEventState>) {
            updateMeltdownEvent(world, state, event.remaining_time, deltaTime);
        } else if constexpr (std::is_same_v<T, RainEventState>) {
            updateRainEvent(world, state, deltaTime);
        }
    }, event.state);
}

void ClockScenario::updateRainEvent(World& world, RainEventState& /*state*/, double deltaTime)
{
    // Spawn water drops at random X positions near the top.
    constexpr double DROPS_PER_SECOND = 10.0;
    double drop_probability = DROPS_PER_SECOND * deltaTime;

    if (uniform_dist_(rng_) < drop_probability) {
        std::uniform_int_distribution<uint32_t> x_dist(2, world.getData().width - 3);
        uint32_t x = x_dist(rng_);
        uint32_t y = 2;

        world.addMaterialAtCell(x, y, MaterialType::WATER, 0.5);
    }

    // Water drainage is handled by updateDrain() in tick().
}

void ClockScenario::spawnDuck(World& world, DuckEventState& state)
{
    // Use DuckBrain2 with dead reckoning and exit-seeking behavior.
    std::unique_ptr<DuckBrain> brain = std::make_unique<DuckBrain2>();

    // Spawn duck in the door opening.
    uint32_t duck_x = static_cast<uint32_t>(state.entrance_door_pos.x);
    uint32_t duck_y = static_cast<uint32_t>(state.entrance_door_pos.y);
    state.organism_id = world.getOrganismManager().createDuck(world, duck_x, duck_y, std::move(brain));

    spdlog::info("ClockScenario: Duck organism {} enters through {} door at ({}, {})",
        state.organism_id,
        state.entrance_side == DoorSide::LEFT ? "LEFT" : "RIGHT",
        duck_x, duck_y);

    state.phase = DuckEventPhase::DUCK_ACTIVE;
}

void ClockScenario::updateDuckEvent(World& world, DuckEventState& state, double& remaining_time, double deltaTime)
{
    // Phase 1: Wait for door to be open a bit before spawning duck.
    constexpr double DOOR_OPEN_DELAY = 2.0;

    if (state.phase == DuckEventPhase::DOOR_OPENING) {
        state.door_open_timer += deltaTime;
        if (state.door_open_timer >= DOOR_OPEN_DELAY) {
            spawnDuck(world, state);
        }
        return;
    }

    // Phase 3: Duck exited, wait briefly then close door and end event.
    constexpr double DOOR_CLOSE_DELAY = 1.0;

    if (state.phase == DuckEventPhase::DOOR_CLOSING) {
        state.door_close_timer += deltaTime;
        if (state.door_close_timer >= DOOR_CLOSE_DELAY) {
            // Signal event to end by setting remaining time to zero.
            remaining_time = 0.0;
        }
        return;
    }

    // Phase 2: Duck is active and walking.
    // Get duck organism.
    Duck* duck_organism = world.getOrganismManager().getDuck(state.organism_id);
    if (!duck_organism) return;

    Vector2i duck_cell = duck_organism->getAnchorCell();

    // Get duck's cell COM for sub-cell positioning.
    const WorldData& data = world.getData();
    Vector2d duck_com{ 0.0, 0.0 };
    if (duck_cell.x >= 0 && duck_cell.y >= 0 &&
        static_cast<uint32_t>(duck_cell.x) < data.width &&
        static_cast<uint32_t>(duck_cell.y) < data.height) {
        duck_com = data.at(duck_cell.x, duck_cell.y).com;
    }

    // Close entrance door once duck moves away from it.
    if (state.entrance_door_open && duck_cell != state.entrance_door_pos) {
        door_manager_.closeDoor(state.entrance_door_pos, world);
        state.entrance_door_open = false;
    }

    // Open exit door in the last 7 seconds.
    if (!state.exit_door_open && remaining_time <= 7.0) {
        DoorSide exit_side = (state.entrance_side == DoorSide::LEFT) ? DoorSide::RIGHT : DoorSide::LEFT;
        door_manager_.openDoor(state.exit_door_pos, exit_side, world);
        state.exit_door_open = true;

        // Log world state when exit door opens.
        spdlog::info("ClockScenario: Exit door opened at ({}, {})",
            state.exit_door_pos.x, state.exit_door_pos.y);
        std::string diagram = WorldDiagramGeneratorEmoji::generateEmojiDiagram(world);
        spdlog::info("\n{}", diagram);
    }

    // Check if duck entered the exit door and passed the middle of the cell.
    if (state.exit_door_open && duck_cell == state.exit_door_pos) {
        bool past_middle = (state.entrance_side == DoorSide::LEFT) ? (duck_com.x > 0.0) : (duck_com.x < 0.0);
        if (past_middle) {
            spdlog::info("ClockScenario: Duck exited through door at ({}, {}), COM.x={:.2f}",
                state.exit_door_pos.x, state.exit_door_pos.y, duck_com.x);

            // Remove the duck immediately.
            world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
            state.organism_id = INVALID_ORGANISM_ID;

            // Transition to door closing phase.
            state.phase = DuckEventPhase::DOOR_CLOSING;
            state.door_close_timer = 0.0;
            return;
        }
    }
}

static const char* eventTypeName(ClockEventType type)
{
    switch (type) {
        case ClockEventType::DUCK: return "DUCK";
        case ClockEventType::MELTDOWN: return "MELTDOWN";
        case ClockEventType::RAIN: return "RAIN";
    }
    return "UNKNOWN";
}

void ClockScenario::endEvent(World& world, ClockEventType type, ActiveEvent& event)
{
    spdlog::info("ClockScenario: Ending {} event", eventTypeName(type));

    // Sync config flags.
    if (type == ClockEventType::RAIN) {
        config_.rainEnabled = false;
    }
    else if (type == ClockEventType::DUCK) {
        config_.duckEnabled = false;
    }

    if (type == ClockEventType::MELTDOWN) {
        // Convert any stray metal (fallen digits) to water.
        convertStrayMetalToWater(world);
    }
    else if (type == ClockEventType::DUCK) {
        auto& state = std::get<DuckEventState>(event.state);

        if (state.organism_id != INVALID_ORGANISM_ID) {
            world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
        }

        // Close any open doors.
        if (state.entrance_door_open) {
            door_manager_.closeDoor(state.entrance_door_pos, world);
        }
        if (state.exit_door_open) {
            door_manager_.closeDoor(state.exit_door_pos, world);
        }
    }

    // Set cooldown for this event type.
    const auto& config = DEFAULT_EVENT_CONFIGS.at(type);
    event_cooldowns_[type] = config.cooldown;

    spdlog::info("ClockScenario: Event {} on cooldown for {:.1f}s",
        eventTypeName(type), config.cooldown);
}

void ClockScenario::cancelAllEvents(World& world)
{
    spdlog::info("ClockScenario: Canceling all events");

    for (auto& [type, event] : active_events_) {
        if (type == ClockEventType::DUCK) {
            auto& state = std::get<DuckEventState>(event.state);
            if (state.organism_id != INVALID_ORGANISM_ID) {
                world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
            }
        }
        else if (type == ClockEventType::MELTDOWN) {
            // Clean up any stray metal from interrupted meltdown.
            convertStrayMetalToWater(world);
        }
    }

    // Clear ALL organisms (in case any lingered from ended events or resize).
    // This prevents orphaned organisms when world is reset/resized.
    world.getOrganismManager().clear();

    // Clear config flags.
    config_.rainEnabled = false;
    config_.duckEnabled = false;

    active_events_.clear();
    event_cooldowns_.clear();
    door_manager_.closeAllDoors(world);
}

bool ClockScenario::isMeltdownActive() const
{
    return active_events_.contains(ClockEventType::MELTDOWN);
}

void ClockScenario::updateMeltdownEvent(
    World& world, MeltdownEventState& state, double& remaining_time, double /*deltaTime*/)
{
    // Convert falling METAL to WATER when it crashes (velocity stops or reverses).
    WorldData& data = world.getData();

    // Scan for crashed metal: at or below digit area and not falling (velocity.y <= 0).
    bool any_stray_metal = false;
    for (uint32_t y = static_cast<uint32_t>(state.digit_bottom_y); y < data.height; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type == MaterialType::METAL) {
                // Convert if velocity is zero or going up (crashed/bounced).
                // +y is down, so <= 0 means stopped or bouncing upward.
                if (cell.velocity.y <= 0.0) {
                    cell.replaceMaterial(MaterialType::WATER, cell.fill_ratio);
                } else {
                    // Still falling - not done yet.
                    any_stray_metal = true;
                }
            }
        }
    }

    // End early if all stray metal below digits has been converted.
    // But wait at least 3 seconds for metal to start falling first.
    constexpr double MIN_MELTDOWN_TIME = 3.0;
    double elapsed = DEFAULT_EVENT_CONFIGS.at(ClockEventType::MELTDOWN).duration - remaining_time;

    if (!any_stray_metal && elapsed >= MIN_MELTDOWN_TIME) {
        remaining_time = 0.0;
    }
}

void ClockScenario::convertStrayMetalToWater(World& world)
{
    WorldData& data = world.getData();

    // Convert all metal to water, then redraw fresh digits.
    for (uint32_t y = 1; y < data.height; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type == MaterialType::METAL) {
                cell.replaceMaterial(MaterialType::WATER, cell.fill_ratio);
            }
        }
    }

    drawTime(world);
}

double ClockScenario::countWaterInBottomThird(const World& world) const
{
    const WorldData& data = world.getData();

    // Count water in the bottom 1/3 of the world.
    uint32_t bottom_third_start = (data.height * 2) / 3;
    double total_water = 0.0;

    for (uint32_t y = bottom_third_start; y < data.height - 1; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.material_type == MaterialType::WATER) {
                total_water += cell.fill_ratio;
            }
        }
    }

    return total_water;
}

void ClockScenario::updateDrain(World& world)
{
    WorldData& data = world.getData();
    if (data.height < 3 || data.width < 5) return;

    // Count water in bottom third.
    double water_amount = countWaterInBottomThird(world);

    // Thresholds for drain opening size.
    constexpr double CLOSE_THRESHOLD = 1.0;   // Below this, drain is closed.
    constexpr double FULL_OPEN_THRESHOLD = 100.0;  // At or above this, drain is fully open.
    constexpr uint32_t MAX_DRAIN_SIZE = 7;    // Maximum drain opening width.

    // Calculate proportional drain size based on water level (odd numbers only: 1, 3, 5, 7).
    uint32_t target_drain_size = 0;
    if (water_amount >= FULL_OPEN_THRESHOLD) {
        target_drain_size = MAX_DRAIN_SIZE;
    } else if (water_amount >= CLOSE_THRESHOLD) {
        // Linear interpolation from 1 to MAX_DRAIN_SIZE, but quantized to odd numbers only.
        double t = (water_amount - CLOSE_THRESHOLD) / (FULL_OPEN_THRESHOLD - CLOSE_THRESHOLD);
        uint32_t continuous_size = 1 + static_cast<uint32_t>(t * (MAX_DRAIN_SIZE - 1));

        // Round to nearest odd number (1, 3, 5, 7).
        if (continuous_size % 2 == 0) {
            // Even number - round down to next lower odd number.
            target_drain_size = continuous_size - 1;
        } else {
            target_drain_size = continuous_size;
        }
    }
    // else target_drain_size remains 0 (closed).

    uint32_t center_x = data.width / 2;
    uint32_t drain_y = data.height - 1;  // Bottom wall row.

    // Calculate new drain bounds based on target size (centered).
    uint32_t half_drain = target_drain_size / 2;
    uint32_t new_start_x = (target_drain_size > 0 && center_x > half_drain) ? center_x - half_drain : center_x;
    uint32_t new_end_x = (target_drain_size > 0) ? std::min(new_start_x + target_drain_size - 1, data.width - 2) : 0;

    // Ensure start doesn't go below 1 (keep wall border).
    if (new_start_x < 1) new_start_x = 1;

    // Track if drain size changed.
    bool drain_was_open = drain_open_;
    uint32_t old_start_x = drain_start_x_;
    uint32_t old_end_x = drain_end_x_;

    drain_open_ = (target_drain_size > 0);
    drain_start_x_ = new_start_x;
    drain_end_x_ = new_end_x;

    // Update drain cells if size changed.
    if (drain_was_open || drain_open_) {
        // Restore wall on cells that are no longer in the drain.
        if (drain_was_open) {
            for (uint32_t x = old_start_x; x <= old_end_x; ++x) {
                bool still_open = drain_open_ && x >= new_start_x && x <= new_end_x;
                if (!still_open) {
                    world.replaceMaterialAtCell(x, drain_y, MaterialType::WALL);
                }
            }
        }

        // Open new drain cells.
        if (drain_open_) {
            for (uint32_t x = new_start_x; x <= new_end_x; ++x) {
                bool was_open = drain_was_open && x >= old_start_x && x <= old_end_x;
                if (!was_open) {
                    data.at(x, drain_y) = Cell();
                }
            }
        }

        // Log significant changes.
        if (!drain_was_open && drain_open_) {
            spdlog::info("ClockScenario: Drain opened (size: {}, water: {:.1f})",
                target_drain_size, water_amount);
        } else if (drain_was_open && !drain_open_) {
            spdlog::info("ClockScenario: Drain closed (water: {:.1f})", water_amount);
        }
    }

    // If drain is open, remove any water that falls into the drain cells.
    if (drain_open_) {
        for (uint32_t x = drain_start_x_; x <= drain_end_x_; ++x) {
            Cell& cell = data.at(x, drain_y);
            if (cell.material_type == MaterialType::WATER) {
                cell = Cell();  // Water drains away.
            }
        }

        // Drain center position.
        double drain_center_x = static_cast<double>(drain_start_x_ + drain_end_x_) / 2.0;
        double drain_center_y = static_cast<double>(drain_y);

        // Apply global gravity-like pull toward drain for all water in the world.
        constexpr double DRAIN_GRAVITY = 1.0;  // Gentle pull toward drain.

        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                Cell& cell = data.at(x, y);
                if (cell.material_type != MaterialType::WATER) {
                    continue;
                }

                // Vector from cell to drain center.
                double dx = drain_center_x - static_cast<double>(x);
                double dy = drain_center_y - static_cast<double>(y);

                // Normalize direction.
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 0.5) {
                    dx /= dist;
                    dy /= dist;
                    cell.addPendingForce(Vector2d{ dx * DRAIN_GRAVITY, dy * DRAIN_GRAVITY });
                }
            }
        }

        // Apply stronger suction force to water on the bottom playable row.
        uint32_t bottom_row = drain_y - 1;  // Row above the drain (height - 2).
        double max_distance = static_cast<double>(data.width) / 2.0;
        constexpr double MAX_FORCE = 5.0;  // Maximum suction force.

        for (uint32_t x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, bottom_row);
            if (cell.material_type != MaterialType::WATER) {
                continue;
            }

            // Calculate distance to drain center.
            double cell_x = static_cast<double>(x);
            double distance = std::abs(cell_x - drain_center_x);

            // Force strength: stronger when close.
            double strength = 1.0 - 0.9 * std::min(distance / max_distance, 1.0);
            double force_magnitude = MAX_FORCE * strength;

            // Direction toward drain center.
            double direction = (cell_x < drain_center_x) ? 1.0 : -1.0;
            if (std::abs(cell_x - drain_center_x) < 0.5) {
                direction = 0.0;  // Already at drain.
            }

            cell.addPendingForce(Vector2d{ direction * force_magnitude, 0.0 });
        }
    }
}

void ClockScenario::redrawWalls(World& world)
{
    uint32_t width = world.getData().width;
    uint32_t height = world.getData().height;

    // Top and bottom borders.
    for (uint32_t x = 0; x < width; ++x) {
        Vector2i top_pos{ static_cast<int>(x), 0 };
        Vector2i bottom_pos{ static_cast<int>(x), static_cast<int>(height - 1) };

        if (!door_manager_.isOpenDoor(top_pos)) {
            world.replaceMaterialAtCell(x, 0, MaterialType::WALL);
        }

        // Skip drain cells in bottom wall when drain is open.
        bool is_drain_cell = drain_open_ && x >= drain_start_x_ && x <= drain_end_x_;
        if (!door_manager_.isOpenDoor(bottom_pos) && !is_drain_cell) {
            world.replaceMaterialAtCell(x, height - 1, MaterialType::WALL);
        }
    }

    // Left and right borders.
    for (uint32_t y = 0; y < height; ++y) {
        Vector2i left_pos{ 0, static_cast<int>(y) };
        Vector2i right_pos{ static_cast<int>(width - 1), static_cast<int>(y) };

        if (!door_manager_.isOpenDoor(left_pos)) {
            world.replaceMaterialAtCell(0, y, MaterialType::WALL);
        }
        if (!door_manager_.isOpenDoor(right_pos)) {
            world.replaceMaterialAtCell(width - 1, y, MaterialType::WALL);
        }
    }

    // Ensure roof cells are walls.
    for (const auto& roof_pos : door_manager_.getRoofPositions()) {
        world.replaceMaterialAtCell(roof_pos.x, roof_pos.y, MaterialType::WALL);
    }
}

} // namespace DirtSim
