#include "ClockScenario.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/FragmentationParams.h"
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

ClockScenario::ClockScenario(ClockEventConfigs event_configs)
    : event_configs_(std::move(event_configs))
{
    metadata_.name = "Clock";
    metadata_.description = "Digital clock displaying system time (HH:MM:SS)";
    metadata_.category = "demo";

    recalculateDimensions();
}

bool ClockScenario::isEventActive(ClockEventType type) const
{
    return active_events_.contains(type);
}

size_t ClockScenario::getActiveEventCount() const
{
    return active_events_.size();
}

const EventTimingConfig& ClockScenario::getEventTiming(ClockEventType type) const
{
    switch (type) {
    case ClockEventType::COLOR_CYCLE:
        return event_configs_.color_cycle.timing;
    case ClockEventType::COLOR_SHOWCASE:
        return event_configs_.color_showcase.timing;
    case ClockEventType::DUCK:
        return event_configs_.duck.timing;
    case ClockEventType::MELTDOWN:
        return event_configs_.meltdown.timing;
    case ClockEventType::RAIN:
        return event_configs_.rain.timing;
    }
    // Unreachable, but satisfy compiler.
    return event_configs_.duck.timing;
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

        // Check if layout changed (requires full reset - digit appearance changes).
        bool layout_changed = (incoming.showSeconds != config_.showSeconds) ||
                              (incoming.font != config_.font);

        // Check if only dimensions changed (can handle incrementally).
        bool dimensions_changed = (incoming.autoScale != config_.autoScale) ||
                                  (incoming.targetDisplayWidth != config_.targetDisplayWidth) ||
                                  (incoming.targetDisplayHeight != config_.targetDisplayHeight) ||
                                  (incoming.marginPixels != config_.marginPixels);

        // Track event toggle changes before updating config_.
        bool color_cycle_was_enabled = config_.colorCycleEnabled;
        bool color_showcase_was_enabled = config_.colorShowcaseEnabled;
        bool duck_was_enabled = config_.duckEnabled;
        bool rain_was_enabled = config_.rainEnabled;

        config_ = incoming;

        if (layout_changed) {
            // Layout changes require full reset (digit sizes change).
            recalculateDimensions();

            spdlog::info(
                "ClockScenario: Layout changed, full reset to {}x{} (font={}, showSeconds={})",
                metadata_.requiredWidth,
                metadata_.requiredHeight,
                static_cast<int>(config_.font),
                config_.showSeconds);

            cancelAllEvents(world);
            world.resizeGrid(metadata_.requiredWidth, metadata_.requiredHeight);
            reset(world);
        }
        else if (dimensions_changed) {
            // Dimension-only changes can be handled incrementally.
            // Keep events running - duck can keep walking.
            recalculateDimensions();

            spdlog::info(
                "ClockScenario: Dimensions changed, incremental resize to {}x{} (display={}x{})",
                metadata_.requiredWidth,
                metadata_.requiredHeight,
                config_.targetDisplayWidth,
                config_.targetDisplayHeight);

            // Before resize: Clear digits (they'll be repositioned after resize).
            clearDigits(world);

            world.resizeGrid(metadata_.requiredWidth, metadata_.requiredHeight);

            // After resize: Reset drain state (positions may be invalid now).
            drain_open_ = false;
            drain_start_x_ = 0;
            drain_end_x_ = 0;
            current_drain_size_ = 0;

            // Clear stray WALL cells from interior (old boundaries may now be inside).
            WorldData& data = world.getData();
            for (uint32_t y = 1; y < data.height - 1; ++y) {
                for (uint32_t x = 1; x < data.width - 1; ++x) {
                    if (data.at(x, y).material_type == MaterialType::WALL) {
                        data.at(x, y) = Cell();
                    }
                }
            }

            // Redraw walls at new boundaries and digits at new centered positions.
            redrawWalls(world);
            drawTime(world);
        }

        // Handle manual event toggling.
        // Color cycle event toggle.
        if (config_.colorCycleEnabled && !color_cycle_was_enabled) {
            // User enabled color cycle - start it if not already active.
            if (!active_events_.contains(ClockEventType::COLOR_CYCLE)) {
                event_cooldowns_[ClockEventType::COLOR_CYCLE] = 0.0;  // Clear cooldown.
                startEvent(world, ClockEventType::COLOR_CYCLE);
                spdlog::info("ClockScenario: Color cycle manually enabled");
            }
        }
        else if (!config_.colorCycleEnabled && color_cycle_was_enabled) {
            // User disabled color cycle - stop it if active.
            auto it = active_events_.find(ClockEventType::COLOR_CYCLE);
            if (it != active_events_.end()) {
                endEvent(world, ClockEventType::COLOR_CYCLE, it->second);
                active_events_.erase(it);
                event_cooldowns_[ClockEventType::COLOR_CYCLE] = 0.0;  // No cooldown for manual stop.
                spdlog::info("ClockScenario: Color cycle manually disabled");
            }
        }

        // Color showcase event toggle.
        if (config_.colorShowcaseEnabled && !color_showcase_was_enabled) {
            // User enabled color showcase - start it if not already active.
            if (!active_events_.contains(ClockEventType::COLOR_SHOWCASE)) {
                event_cooldowns_[ClockEventType::COLOR_SHOWCASE] = 0.0;  // Clear cooldown.
                startEvent(world, ClockEventType::COLOR_SHOWCASE);
                spdlog::info("ClockScenario: Color showcase manually enabled");
            }
        }
        else if (!config_.colorShowcaseEnabled && color_showcase_was_enabled) {
            // User disabled color showcase - stop it if active.
            auto it = active_events_.find(ClockEventType::COLOR_SHOWCASE);
            if (it != active_events_.end()) {
                endEvent(world, ClockEventType::COLOR_SHOWCASE, it->second);
                active_events_.erase(it);
                event_cooldowns_[ClockEventType::COLOR_SHOWCASE] = 0.0;  // No cooldown for manual stop.
                spdlog::info("ClockScenario: Color showcase manually disabled");
            }
        }

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
    cancelAllEvents(world);
    setup(world);
}

void ClockScenario::tick(World& world, double deltaTime)
{
    redrawWalls(world);

    if (!isMeltdownActive() && !isEventActive(ClockEventType::COLOR_CYCLE)) {
        drawTime(world);
    }

    // Update event system (may clear floor obstacles).
    updateEvents(world, deltaTime);

    // Manage floor drain based on water level.
    // Runs after events so drain clearing catches any floor restored by obstacle clearing.
    updateDrain(world, deltaTime);

    // Debug check: verify all WOOD cells have an associated organism.
    // WOOD cells only come from ducks in this scenario, so orphaned WOOD is a bug.
    const WorldData& data = world.getData();
    const auto& org_grid = world.getOrganismManager().getGrid();
    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            size_t idx = y * data.width + x;
            if (data.cells[idx].material_type == MaterialType::WOOD) {
                if (org_grid[idx] == INVALID_ORGANISM_ID) {
                    spdlog::error("ClockScenario: Orphaned WOOD cell at ({}, {}) with no organism!",
                        x, y);
                    DIRTSIM_ASSERT(false, "Orphaned WOOD cell found - see log for details");
                }
            }
        }
    }
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

void ClockScenario::clearDigits(World& world)
{
    WorldData& data = world.getData();

    // Get floor obstacles from active duck event (if any).
    const std::vector<FloorObstacle>* floor_obstacles = nullptr;
    auto duck_it = active_events_.find(ClockEventType::DUCK);
    if (duck_it != active_events_.end()) {
        const auto& duck_state = std::get<DuckEventState>(duck_it->second.state);
        floor_obstacles = &duck_state.floor_obstacles;
    }

    // Helper to check if X position is a hurdle (one row above floor).
    auto is_hurdle_at = [&](uint32_t x, uint32_t y) -> bool {
        if (!floor_obstacles || y != data.height - 2) return false;
        for (const auto& obs : *floor_obstacles) {
            if (obs.type == FloorObstacleType::HURDLE) {
                if (static_cast<int>(x) >= obs.start_x &&
                    static_cast<int>(x) < obs.start_x + obs.width) {
                    return true;
                }
            }
        }
        return false;
    };

    // Clear interior WALL cells (digit cells) but NOT:
    // - Boundary cells (x=0, x=width-1, y=0, y=height-1)
    // - Door roof cells
    // - Hurdle obstacle cells
    for (uint32_t y = 1; y < data.height - 1; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type != MaterialType::WALL) {
                continue;
            }

            // Skip door roof cells.
            Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };
            if (door_manager_.isRoofCell(pos)) {
                continue;
            }

            // Skip hurdle obstacle cells.
            if (is_hurdle_at(x, y)) {
                continue;
            }

            // This is a digit cell - clear it.
            cell = Cell();
        }
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
                // Use WALL (immobile) but render as the configured digit material.
                world.replaceMaterialAtCell(x, y, MaterialType::WALL);
                world.getData().at(x, y).render_as = static_cast<int8_t>(config_.digitMaterial);
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
                // Use WALL (immobile) but render as the configured digit material.
                world.replaceMaterialAtCell(x, y1, MaterialType::WALL);
                world.getData().at(x, y1).render_as = static_cast<int8_t>(config_.digitMaterial);
            }
            if (y2 >= 0 && y2 < static_cast<int>(world.getData().height)) {
                // Use WALL (immobile) but render as the configured digit material.
                world.replaceMaterialAtCell(x, y2, MaterialType::WALL);
                world.getData().at(x, y2).render_as = static_cast<int8_t>(config_.digitMaterial);
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

    // Clear previous digits using tracked positions.
    clearDigits(world);

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

    // Update last drawn time for change detection.
    last_drawn_time_ = getCurrentTimeString();
}

std::string ClockScenario::getCurrentTimeString() const
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm* time_info;
    if (config_.timezoneIndex == 0) {
        time_info = std::localtime(&now_time);
    }
    else {
        time_info = std::gmtime(&now_time);
        const auto& tz = TIMEZONES[config_.timezoneIndex];
        time_info->tm_hour += tz.offset_hours;
        if (time_info->tm_hour < 0) {
            time_info->tm_hour += 24;
        }
        else if (time_info->tm_hour >= 24) {
            time_info->tm_hour -= 24;
        }
    }

    char buffer[16];
    if (config_.showSeconds) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
            time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d",
            time_info->tm_hour, time_info->tm_min);
    }
    return std::string(buffer);
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
    static constexpr std::array<ClockEventType, 5> ALL_EVENT_TYPES = {
        ClockEventType::COLOR_CYCLE,
        ClockEventType::COLOR_SHOWCASE,
        ClockEventType::DUCK,
        ClockEventType::MELTDOWN,
        ClockEventType::RAIN,
    };

    // Check each event type for triggering.
    for (ClockEventType type : ALL_EVENT_TYPES) {
        // Skip if already active.
        if (active_events_.contains(type)) {
            continue;
        }

        // Skip if on cooldown.
        if (event_cooldowns_.contains(type) && event_cooldowns_[type] > 0.0) {
            continue;
        }

        // Scale chance by eventFrequency config.
        const auto& timing = getEventTiming(type);
        double effective_chance = timing.chance_per_second * config_.eventFrequency;

        if (uniform_dist_(rng_) < effective_chance) {
            startEvent(world, type);
        }
    }
}

void ClockScenario::startEvent(World& world, ClockEventType type)
{
    const auto& eventTiming = getEventTiming(type);

    ActiveEvent event;
    event.remaining_time = eventTiming.duration;

    if (type == ClockEventType::COLOR_CYCLE) {
        ColorCycleEventState state;
        // Use config's colorsPerSecond rate.
        state.time_per_color = 1.0 / config_.colorsPerSecond;
        state.current_index = 0;
        state.time_in_current = 0.0;

        // Apply the first color immediately.
        MaterialType first_material = getAllMaterialTypes()[0];
        WorldData& data = world.getData();
        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                Cell& cell = data.at(x, y);
                if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                    cell.render_as = static_cast<int8_t>(first_material);
                }
            }
        }

        event.state = state;
        config_.colorCycleEnabled = true;  // Sync config flag.
        spdlog::info("ClockScenario: Starting COLOR_CYCLE event (duration: {}s, rate: {} colors/sec)",
            eventTiming.duration, config_.colorsPerSecond);
    }
    else if (type == ClockEventType::MELTDOWN) {
        MeltdownEventState melt_state;
        // Use METAL for falling digits - dense, falls through water nicely.
        // The UI digitMaterial setting only affects render color, not physics.
        melt_state.digit_material = MaterialType::METAL;
        WorldData& data = world.getData();

        // Convert interior WALL cells (digit display cells) to METAL so they can fall.
        // These are WALL cells with render_as set (indicating they are digit cells).
        int max_digit_y = 0;
        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                Cell& cell = data.at(x, y);

                // Only convert WALL cells with render_as override (digit cells).
                if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                    // Convert to the digit material so it can fall.
                    cell.material_type = melt_state.digit_material;
                    cell.render_as = -1;  // Clear override - now it's the real material.
                    max_digit_y = std::max(max_digit_y, static_cast<int>(y));
                }
            }
        }
        melt_state.digit_bottom_y = max_digit_y;

        event.state = melt_state;
        spdlog::info("ClockScenario: Starting MELTDOWN event (duration: {}s, digit_bottom_y: {}, material: {})",
            eventTiming.duration, melt_state.digit_bottom_y, getMaterialName(melt_state.digit_material));
    }
    else if (type == ClockEventType::RAIN) {
        event.state = RainEventState{};
        config_.rainEnabled = true;  // Sync config flag.
        spdlog::info("ClockScenario: Starting RAIN event (duration: {}s)", eventTiming.duration);
    }
    else if (type == ClockEventType::COLOR_SHOWCASE) {
        ColorShowcaseEventState state;

        // Start on a random color each time.
        const auto& showcase_materials = event_configs_.color_showcase.showcase_materials;
        if (!showcase_materials.empty()) {
            std::uniform_int_distribution<size_t> color_dist(0, showcase_materials.size() - 1);
            state.current_index = color_dist(rng_);
            config_.digitMaterial = showcase_materials[state.current_index];
            spdlog::info("ClockScenario: Starting COLOR_SHOWCASE event (duration: {}s, starting color: {} at index {})",
                eventTiming.duration, getMaterialName(showcase_materials[state.current_index]), state.current_index);
        }

        event.state = state;
        config_.colorShowcaseEnabled = true;  // Sync config flag.
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
        spdlog::info("ClockScenario: Starting DUCK event (duration: {}s)", eventTiming.duration);
    }

    active_events_[type] = std::move(event);
}

void ClockScenario::updateEvent(World& world, ClockEventType /*type*/, ActiveEvent& event, double deltaTime)
{
    std::visit([&](auto& state) {
        using T = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<T, ColorCycleEventState>) {
            updateColorCycleEvent(world, state, deltaTime);
        } else if constexpr (std::is_same_v<T, ColorShowcaseEventState>) {
            updateColorShowcaseEvent(world, state, deltaTime);
        } else if constexpr (std::is_same_v<T, DuckEventState>) {
            updateDuckEvent(world, state, event.remaining_time, deltaTime);
        } else if constexpr (std::is_same_v<T, MeltdownEventState>) {
            updateMeltdownEvent(world, state, event.remaining_time, deltaTime);
        } else if constexpr (std::is_same_v<T, RainEventState>) {
            updateRainEvent(world, state, deltaTime);
        }
    }, event.state);
}

void ClockScenario::updateColorCycleEvent(World& world, ColorCycleEventState& state, double deltaTime)
{
    state.time_in_current += deltaTime;

    // Check if it's time to advance to the next color.
    if (state.time_in_current >= state.time_per_color) {
        state.time_in_current -= state.time_per_color;
        state.current_index = (state.current_index + 1) % getAllMaterialTypes().size();

        MaterialType new_material = getAllMaterialTypes()[state.current_index];

        // Update all digit cells to the new color.
        WorldData& data = world.getData();
        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                Cell& cell = data.at(x, y);
                // Digit cells are WALL with render_as override set.
                if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                    cell.render_as = static_cast<int8_t>(new_material);
                }
            }
        }

        spdlog::debug("ClockScenario: COLOR_CYCLE advanced to {} (index {})",
            getMaterialName(new_material), state.current_index);
    }
}

void ClockScenario::updateColorShowcaseEvent(World& /*world*/, ColorShowcaseEventState& state, double /*deltaTime*/)
{
    // Check if time string changed (digits will be redrawn).
    std::string current_time = getCurrentTimeString();
    if (current_time != last_drawn_time_) {
        // Advance to next showcase color.
        const auto& showcase_materials = event_configs_.color_showcase.showcase_materials;
        if (!showcase_materials.empty()) {
            state.current_index = (state.current_index + 1) % showcase_materials.size();
            MaterialType new_material = showcase_materials[state.current_index];
            config_.digitMaterial = new_material;

            spdlog::info("ClockScenario: COLOR_SHOWCASE changed to {} (time changed to {})",
                getMaterialName(new_material), current_time);
        }
    }
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

    // Floor obstacles: spawn when drain is closed and obstacles are enabled.
    if (!drain_open_ && event_configs_.duck.floor_obstacles_enabled) {
        constexpr double SPAWN_INTERVAL = 3.0;  // Try to spawn every 3 seconds.
        constexpr size_t MAX_OBSTACLES = 3;     // Maximum concurrent obstacles.

        state.obstacle_spawn_timer += deltaTime;
        if (state.obstacle_spawn_timer >= SPAWN_INTERVAL && state.floor_obstacles.size() < MAX_OBSTACLES) {
            state.obstacle_spawn_timer = 0.0;
            spawnFloorObstacle(world, state);
        }
    } else if (drain_open_) {
        // Drain is open, clear any obstacles.
        if (!state.floor_obstacles.empty()) {
            clearFloorObstacles(world, state);
        }
    }

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
        case ClockEventType::COLOR_CYCLE: return "COLOR_CYCLE";
        case ClockEventType::COLOR_SHOWCASE: return "COLOR_SHOWCASE";
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
    if (type == ClockEventType::COLOR_CYCLE) {
        config_.colorCycleEnabled = false;
    }
    else if (type == ClockEventType::COLOR_SHOWCASE) {
        config_.colorShowcaseEnabled = false;
    }
    else if (type == ClockEventType::DUCK) {
        config_.duckEnabled = false;
    }
    else if (type == ClockEventType::RAIN) {
        config_.rainEnabled = false;
    }

    if (type == ClockEventType::COLOR_CYCLE) {
        // Restore original digit material color.
        WorldData& data = world.getData();
        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                Cell& cell = data.at(x, y);
                if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                    cell.render_as = static_cast<int8_t>(config_.digitMaterial);
                }
            }
        }
    }
    else if (type == ClockEventType::MELTDOWN) {
        // Convert any stray digit material (fallen digits) to water.
        auto& melt_state = std::get<MeltdownEventState>(event.state);
        convertStrayDigitMaterialToWater(world, melt_state.digit_material);
    }
    else if (type == ClockEventType::DUCK) {
        auto& state = std::get<DuckEventState>(event.state);

        if (state.organism_id != INVALID_ORGANISM_ID) {
            world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
        }

        // Clear any floor obstacles.
        clearFloorObstacles(world, state);

        // Close any open doors.
        if (state.entrance_door_open) {
            door_manager_.closeDoor(state.entrance_door_pos, world);
        }
        if (state.exit_door_open) {
            door_manager_.closeDoor(state.exit_door_pos, world);
        }
    }
    else if (type == ClockEventType::COLOR_SHOWCASE) {
        // Restore default digit material (METAL).
        config_.digitMaterial = MaterialType::METAL;
        spdlog::info("ClockScenario: COLOR_SHOWCASE ended, restored digit material to METAL");
    }

    // Set cooldown for this event type.
    const auto& timing = getEventTiming(type);
    event_cooldowns_[type] = timing.cooldown;

    spdlog::info("ClockScenario: Event {} on cooldown for {:.1f}s",
        eventTypeName(type), timing.cooldown);
}

void ClockScenario::cancelAllEvents(World& world)
{
    spdlog::info("ClockScenario: Canceling all events");

    for (auto& [type, event] : active_events_) {
        if (type == ClockEventType::COLOR_CYCLE) {
            // Restore original digit material color.
            WorldData& data = world.getData();
            for (uint32_t y = 1; y < data.height - 1; ++y) {
                for (uint32_t x = 1; x < data.width - 1; ++x) {
                    Cell& cell = data.at(x, y);
                    if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                        cell.render_as = static_cast<int8_t>(config_.digitMaterial);
                    }
                }
            }
        }
        else if (type == ClockEventType::DUCK) {
            auto& state = std::get<DuckEventState>(event.state);
            if (state.organism_id != INVALID_ORGANISM_ID) {
                world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
            }
            // Clear any floor obstacles.
            clearFloorObstacles(world, state);
        }
        else if (type == ClockEventType::MELTDOWN) {
            // Clean up any stray digit material from interrupted meltdown.
            auto& melt_state = std::get<MeltdownEventState>(event.state);
            convertStrayDigitMaterialToWater(world, melt_state.digit_material);
        }
    }

    // Clear ALL organisms (in case any lingered from ended events or resize).
    // This prevents orphaned organisms when world is reset/resized.
    world.getOrganismManager().clear();

    // Clear config flags.
    config_.colorCycleEnabled = false;
    config_.colorShowcaseEnabled = false;
    config_.duckEnabled = false;
    config_.rainEnabled = false;

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
    WorldData& data = world.getData();
    if (data.height < 3) return;

    uint32_t bottom_wall_y = data.height - 1;
    uint32_t above_bottom_y = data.height - 2;
    MaterialType digit_mat = state.digit_material;

    // Fragmentation params for digit material spraying up from drain.
    static const FragmentationParams melt_frag_params{
        .radial_bias = 0.3,
        .min_arc = M_PI / 4.0,
        .max_arc = M_PI / 3.0,
        .edge_speed_factor = 1.0,
        .base_speed = 40.0,
        .spray_fraction = 1.0,
    };

    // Scan for digit material that has reached the bottom or fallen into drain.
    bool any_digit_material_above_bottom = false;

    Vector2d spray_direction(0.0, -1.0);
    constexpr int NUM_FRAGS = 4;
    constexpr double ARC_WIDTH = M_PI / 2.0;

    for (uint32_t x = 1; x < data.width - 1; ++x) {
        // Check cells in drain hole (bottom wall row, if drain is open).
        if (drain_open_ && x >= drain_start_x_ && x <= drain_end_x_) {
            Cell& drain_cell = data.at(x, bottom_wall_y);
            if (drain_cell.material_type == digit_mat) {
                // Convert to water and spray upward.
                drain_cell.replaceMaterial(MaterialType::WATER, drain_cell.fill_ratio);

                world.getCollisionCalculator().fragmentSingleCell(
                    world,
                    drain_cell,
                    x,
                    bottom_wall_y,
                    x,
                    bottom_wall_y,
                    spray_direction,
                    NUM_FRAGS,
                    ARC_WIDTH,
                    melt_frag_params);

                drain_cell = Cell();
            }
        }

        // Check cells adjacent to bottom wall (row above it).
        Cell& bottom_cell = data.at(x, above_bottom_y);
        if (bottom_cell.material_type == digit_mat) {
            // Convert to water and splash upward.
            bottom_cell.replaceMaterial(MaterialType::WATER, bottom_cell.fill_ratio);

            world.getCollisionCalculator().fragmentSingleCell(
                world,
                bottom_cell,
                x,
                above_bottom_y,
                x,
                above_bottom_y,
                spray_direction,
                NUM_FRAGS,
                ARC_WIDTH,
                melt_frag_params);

            bottom_cell = Cell();
        }
    }

    // Check if any digit material still exists above the bottom row (still falling).
    for (uint32_t y = 1; y < above_bottom_y; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            if (data.at(x, y).material_type == digit_mat) {
                any_digit_material_above_bottom = true;
                break;
            }
        }
        if (any_digit_material_above_bottom) break;
    }

    // End early if all digit material has reached the bottom.
    // But wait at least 3 seconds for material to start falling first.
    constexpr double MIN_MELTDOWN_TIME = 3.0;
    double elapsed = getEventTiming(ClockEventType::MELTDOWN).duration - remaining_time;

    if (!any_digit_material_above_bottom && elapsed >= MIN_MELTDOWN_TIME) {
        remaining_time = 0.0;
    }
}

void ClockScenario::convertStrayDigitMaterialToWater(World& world, MaterialType digit_material)
{
    WorldData& data = world.getData();

    // Convert all digit material to water, then redraw fresh digits.
    for (uint32_t y = 1; y < data.height; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type == digit_material) {
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

void ClockScenario::updateDrain(World& world, double deltaTime)
{
    WorldData& data = world.getData();
    if (data.height < 3 || data.width < 5) return;

    // Count water in bottom third.
    double water_amount = countWaterInBottomThird(world);

    // Thresholds for drain opening size.
    constexpr double CLOSE_THRESHOLD = 1.0;   // Below this, drain is closed.
    constexpr double FULL_OPEN_THRESHOLD = 100.0;  // At or above this, drain is fully open.
    constexpr uint32_t MAX_DRAIN_SIZE = 7;    // Maximum drain opening width.

    // Calculate target drain size based on water level (odd numbers only: 3, 5, 7).
    // Size 1 is only used as an animation transition step, not a sustained state.
    uint32_t target_drain_size = 0;
    if (water_amount >= FULL_OPEN_THRESHOLD) {
        target_drain_size = MAX_DRAIN_SIZE;
    } else if (water_amount >= CLOSE_THRESHOLD) {
        // Linear interpolation from 3 to MAX_DRAIN_SIZE, quantized to odd numbers.
        double t = (water_amount - CLOSE_THRESHOLD) / (FULL_OPEN_THRESHOLD - CLOSE_THRESHOLD);
        uint32_t continuous_size = 3 + static_cast<uint32_t>(t * (MAX_DRAIN_SIZE - 3));

        // Round to nearest odd number (3, 5, 7).
        if (continuous_size % 2 == 0) {
            // Even number - round down to next lower odd number.
            target_drain_size = continuous_size - 1;
        } else {
            target_drain_size = continuous_size;
        }
    }
    // else target_drain_size remains 0 (closed).

    // Hysteresis: only change drain size one step per second.
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_drain_size_change_).count();

    uint32_t actual_drain_size = current_drain_size_;
    if (target_drain_size != current_drain_size_ && elapsed >= 1000) {
        // Step one size at a time (0 <-> 1 <-> 3 <-> 5 <-> 7).
        if (target_drain_size > current_drain_size_) {
            // Opening: step up.
            if (current_drain_size_ == 0) {
                actual_drain_size = 1;
            } else {
                actual_drain_size = current_drain_size_ + 2;
            }
        } else {
            // Closing: step down.
            if (current_drain_size_ == 1) {
                actual_drain_size = 0;
            } else {
                actual_drain_size = current_drain_size_ - 2;
            }
        }
        current_drain_size_ = actual_drain_size;
        last_drain_size_change_ = now;
    }

    uint32_t center_x = data.width / 2;
    uint32_t drain_y = data.height - 1;  // Bottom wall row.

    // Calculate new drain bounds based on actual size (centered).
    uint32_t half_drain = actual_drain_size / 2;
    uint32_t new_start_x = (actual_drain_size > 0 && center_x > half_drain) ? center_x - half_drain : center_x;
    uint32_t new_end_x = (actual_drain_size > 0) ? std::min(new_start_x + actual_drain_size - 1, data.width - 2) : 0;

    // Ensure start doesn't go below 1 (keep wall border).
    if (new_start_x < 1) new_start_x = 1;

    // Track if drain size changed.
    bool drain_was_open = drain_open_;
    uint32_t old_start_x = drain_start_x_;
    uint32_t old_end_x = drain_end_x_;

    drain_open_ = (actual_drain_size > 0);
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

        // Ensure drain cells are clear (always, not just when newly opened).
        // This handles cases where obstacle clearing may have restored floor over drain.
        if (drain_open_) {
            for (uint32_t x = new_start_x; x <= new_end_x; ++x) {
                Cell& cell = data.at(x, drain_y);
                if (cell.material_type == MaterialType::WALL) {
                    cell = Cell();
                }
            }
        }

        // Log significant changes.
        if (!drain_was_open && drain_open_) {
            spdlog::info("ClockScenario: Drain opened (size: {}, water: {:.1f})",
                actual_drain_size, water_amount);
        } else if (drain_was_open && !drain_open_) {
            spdlog::info("ClockScenario: Drain closed (water: {:.1f})", water_amount);
        }
    }

    // If drain is open, handle material in drain cells.
    if (drain_open_) {
        uint32_t center_x = (drain_start_x_ + drain_end_x_) / 2;

        // Get the digit material if a meltdown is active.
        MaterialType melt_digit_material = MaterialType::AIR;  // Default (won't match anything).
        if (isMeltdownActive()) {
            auto it = active_events_.find(ClockEventType::MELTDOWN);
            if (it != active_events_.end()) {
                melt_digit_material = std::get<MeltdownEventState>(it->second.state).digit_material;
            }
        }

        for (uint32_t x = drain_start_x_; x <= drain_end_x_; ++x) {
            Cell& cell = data.at(x, drain_y);

            // Digit material falls through the drain during meltdown - convert to water and spray.
            if (cell.material_type == melt_digit_material && cell.com.y > 0.0) {
                cell.replaceMaterial(MaterialType::WATER, cell.fill_ratio);
                sprayDrainCell(world, cell, x, drain_y);
                continue;
            }

            if (cell.material_type != MaterialType::WATER) {
                continue;
            }

            const bool com_below_midline = cell.com.y > 0.0;
            if (!com_below_midline) {
                continue;
            }

            const bool is_center = (x == center_x);

            // Center cell: chance to spray dramatically.
            if (is_center && cell.fill_ratio > 0.5) {
                if (uniform_dist_(rng_) < 0.7) {
                    sprayDrainCell(world, cell, x, drain_y);
                    continue;
                }
            }

            // All drain cells dissipate: full to empty in 1/4 second.
            cell.fill_ratio -= (deltaTime * 4);
            if (cell.fill_ratio <= 0.0) {
                cell = Cell();
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

            // Pull water down when directly over the drain opening.
            bool over_drain = (x >= drain_start_x_ && x <= drain_end_x_);
            double downward_force = over_drain ? MAX_FORCE : 0.0;

            cell.addPendingForce(Vector2d{ direction * force_magnitude, downward_force });
        }
    }
}

void ClockScenario::sprayDrainCell(World& world, Cell& cell, uint32_t x, uint32_t y)
{
    static const FragmentationParams drain_frag_params{
        .radial_bias = 0.2,
        .min_arc = M_PI / 3.0,
        .max_arc = M_PI / 2.0,
        .edge_speed_factor = 1.2,
        .base_speed = 50.0,
        .spray_fraction = 1.0,
    };

    Vector2d spray_direction(0.0, -1.0);
    constexpr int NUM_FRAGS = 5;
    constexpr double ARC_WIDTH = M_PI / 2.0;

    world.getCollisionCalculator().fragmentSingleCell(
        world,
        cell,
        x,
        y,
        x,
        y,
        spray_direction,
        NUM_FRAGS,
        ARC_WIDTH,
        drain_frag_params);

    cell = Cell();
}

void ClockScenario::redrawWalls(World& world)
{
    uint32_t width = world.getData().width;
    uint32_t height = world.getData().height;

    // Get floor obstacles from active duck event (if any).
    const std::vector<FloorObstacle>* floor_obstacles = nullptr;
    auto duck_it = active_events_.find(ClockEventType::DUCK);
    if (duck_it != active_events_.end()) {
        const auto& duck_state = std::get<DuckEventState>(duck_it->second.state);
        floor_obstacles = &duck_state.floor_obstacles;
    }

    // Helper to check if X position is a pit.
    auto is_pit_at = [&](uint32_t x) -> bool {
        if (!floor_obstacles) return false;
        for (const auto& obs : *floor_obstacles) {
            if (obs.type == FloorObstacleType::PIT) {
                if (static_cast<int>(x) >= obs.start_x &&
                    static_cast<int>(x) < obs.start_x + obs.width) {
                    return true;
                }
            }
        }
        return false;
    };

    // Helper to check if X position is a hurdle.
    auto is_hurdle_at = [&](uint32_t x) -> bool {
        if (!floor_obstacles) return false;
        for (const auto& obs : *floor_obstacles) {
            if (obs.type == FloorObstacleType::HURDLE) {
                if (static_cast<int>(x) >= obs.start_x &&
                    static_cast<int>(x) < obs.start_x + obs.width) {
                    return true;
                }
            }
        }
        return false;
    };

    // Top and bottom borders.
    for (uint32_t x = 0; x < width; ++x) {
        Vector2i top_pos{ static_cast<int>(x), 0 };
        Vector2i bottom_pos{ static_cast<int>(x), static_cast<int>(height - 1) };

        if (!door_manager_.isOpenDoor(top_pos)) {
            world.replaceMaterialAtCell(x, 0, MaterialType::WALL);
        }

        // Skip drain cells in bottom wall when drain is open.
        bool is_drain_cell = drain_open_ && x >= drain_start_x_ && x <= drain_end_x_;
        // Skip pit cells (floor removed).
        bool is_pit_cell = is_pit_at(x);

        if (!door_manager_.isOpenDoor(bottom_pos) && !is_drain_cell && !is_pit_cell) {
            world.replaceMaterialAtCell(x, height - 1, MaterialType::WALL);
        } else if (is_pit_cell && !is_drain_cell) {
            // Ensure pit cells are cleared.
            Cell& cell = world.getData().at(x, height - 1);
            if (cell.material_type == MaterialType::WALL) {
                cell = Cell();
            }
        }

        // Hurdle cells (one row above floor).
        if (height > 2 && is_hurdle_at(x)) {
            world.replaceMaterialAtCell(x, height - 2, MaterialType::WALL);
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

// ============================================================================
// Floor Obstacle System for Duck Event
// ============================================================================

void ClockScenario::spawnFloorObstacle(World& world, DuckEventState& state)
{
    const WorldData& data = world.getData();

    // Require minimum world size: 5 cells from each wall means we need at least 11 cells wide.
    constexpr int MARGIN = 5;
    int min_x = MARGIN;
    int max_x = static_cast<int>(data.width) - MARGIN - 1;

    // Not enough horizontal space for obstacles.
    if (max_x <= min_x) {
        spdlog::info("ClockScenario: World too narrow for floor obstacles (width={})", data.width);
        return;
    }

    // Choose how many contiguous cells (1-3).
    std::uniform_int_distribution<int> width_dist(1, 3);
    int obstacle_width = width_dist(rng_);

    // Adjust max_x to account for obstacle width.
    int spawn_max_x = max_x - (obstacle_width - 1);
    if (spawn_max_x < min_x) {
        return;  // Not enough space.
    }

    // Pick starting X position.
    std::uniform_int_distribution<int> x_dist(min_x, spawn_max_x);
    int start_x = x_dist(rng_);

    // Choose type: hurdle (up) or pit (down).
    FloorObstacleType type = (uniform_dist_(rng_) < 0.5)
        ? FloorObstacleType::HURDLE
        : FloorObstacleType::PIT;

    // Check for overlap with existing obstacles.
    for (const auto& existing : state.floor_obstacles) {
        int existing_end = existing.start_x + existing.width;
        int new_end = start_x + obstacle_width;
        // Check if ranges overlap.
        if (start_x < existing_end && new_end > existing.start_x) {
            spdlog::debug("ClockScenario: Floor obstacle spawn skipped - overlaps existing");
            return;
        }
    }

    // Create the obstacle.
    FloorObstacle obstacle;
    obstacle.start_x = start_x;
    obstacle.width = obstacle_width;
    obstacle.type = type;

    state.floor_obstacles.push_back(obstacle);

    spdlog::info("ClockScenario: Spawned floor {} at x={}, width={}",
        type == FloorObstacleType::HURDLE ? "HURDLE" : "PIT",
        start_x, obstacle_width);
}

void ClockScenario::clearFloorObstacles(World& world, DuckEventState& state)
{
    if (state.floor_obstacles.empty()) {
        return;
    }

    WorldData& data = world.getData();
    uint32_t height = data.height;

    spdlog::info("ClockScenario: Clearing {} floor obstacles", state.floor_obstacles.size());

    for (const auto& obs : state.floor_obstacles) {
        for (int i = 0; i < obs.width; ++i) {
            int x = obs.start_x + i;
            if (x < 0 || x >= static_cast<int>(data.width)) {
                continue;
            }

            if (obs.type == FloorObstacleType::HURDLE) {
                // Clear hurdle wall at height-2.
                if (height > 2) {
                    Cell& cell = data.at(x, height - 2);
                    if (cell.material_type == MaterialType::WALL) {
                        cell = Cell();
                    }
                }
            } else {
                // Restore floor at height-1 for pit.
                world.replaceMaterialAtCell(x, height - 1, MaterialType::WALL);
            }
        }
    }

    state.floor_obstacles.clear();
}

} // namespace DirtSim
