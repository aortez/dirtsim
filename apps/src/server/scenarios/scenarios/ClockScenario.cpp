#include "ClockScenario.h"
#include "core/Cell.h"
#include "core/Entity.h"
#include "core/MaterialMove.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldCollisionCalculator.h"
#include "core/WorldData.h"
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
    { ClockEventType::RAIN, { .duration = 10.0, .chance_per_second = 0.03, .cooldown = 15.0 } },
    { ClockEventType::DUCK, { .duration = 30.0, .chance_per_second = 0.05, .cooldown = 20.0 } },
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

    // Place wall at roof position.
    Cell& roof_cell = world.getData().at(state.roof_pos.x, state.roof_pos.y);
    roof_cell.replaceMaterial(MaterialType::WALL, 1.0);

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

    // Restore wall at door position.
    Cell& door_cell = world.getData().at(pos.x, pos.y);
    door_cell.replaceMaterial(MaterialType::WALL, 1.0);

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
        materializeWall(world, x, 0);
        materializeWall(world, x, height - 1);
    }

    // Left and right borders.
    for (uint32_t y = 0; y < height; ++y) {
        materializeWall(world, 0, y);
        materializeWall(world, width - 1, y);
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

    // Get current time.
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);

    int current_second = local_time->tm_sec;

    // Only redraw digits if the second has changed.
    if (current_second != last_second_) {
        last_second_ = current_second;
        drawTime(world);
    }

    // Evaporate water from bottom row.
    evaporateBottomRow(world, deltaTime);

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
            case Config::ClockFont::Segment7Large:
                pixel = ClockFonts::SEGMENT7_LARGE_PATTERNS[digit][row][col];
                break;
            case Config::ClockFont::Segment7Tall:
                pixel = ClockFonts::SEGMENT7_TALL_PATTERNS[digit][row][col];
                break;
            }

            if (pixel) {
                materializeWall(world, x, y);
                painted_cells_.push_back(Vector2i{ x, y });
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
                materializeWall(world, x, y1);
                painted_cells_.push_back(Vector2i{ x, y1 });
            }
            if (y2 >= 0 && y2 < static_cast<int>(world.getData().height)) {
                materializeWall(world, x, y2);
                painted_cells_.push_back(Vector2i{ x, y2 });
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

    // Clear only the previously painted clock cells (preserves duck and other entities).
    for (const auto& pos : painted_cells_) {
        if (pos.x >= 0 && pos.x < static_cast<int>(world.getData().width) &&
            pos.y >= 0 && pos.y < static_cast<int>(world.getData().height)) {
            world.getData().at(pos.x, pos.y) = Cell();
        }
    }
    painted_cells_.clear();

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
    const auto& config = DEFAULT_EVENT_CONFIGS.at(type);

    ActiveEvent event;
    event.remaining_time = config.duration;

    if (type == ClockEventType::RAIN) {
        event.state = RainEventState{};
        spdlog::info("ClockScenario: Starting RAIN event (duration: {}s)", config.duration);
    }
    else if (type == ClockEventType::DUCK) {
        DuckEventState duck_state;

        // Use WallBouncingBrain with jumping enabled.
        std::unique_ptr<DuckBrain> brain = std::make_unique<WallBouncingBrain>(true);

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

        // Open entrance door via DoorManager.
        door_manager_.openDoor(duck_state.entrance_door_pos, duck_state.entrance_side, world);

        // Spawn duck just inside the door.
        uint32_t duck_x = (duck_state.entrance_side == DoorSide::LEFT) ? 1 : world.getData().width - 2;
        uint32_t duck_y = door_y;
        duck_state.organism_id = world.getOrganismManager().createDuck(world, duck_x, duck_y, std::move(brain));

        spdlog::info("ClockScenario: Duck organism {} enters through {} door at ({}, {})",
            duck_state.organism_id,
            duck_state.entrance_side == DoorSide::LEFT ? "LEFT" : "RIGHT",
            duck_x, duck_y);

        // Create Entity view for rendering.
        Entity duck_entity;
        duck_entity.id = next_entity_id_++;
        duck_entity.type = EntityType::DUCK;
        duck_entity.visible = true;
        duck_entity.position = Vector2<float>(static_cast<float>(duck_x), static_cast<float>(duck_y));
        duck_entity.com = Vector2<float>(0.0f, 0.0f);
        duck_entity.velocity = Vector2<float>(0.0f, 0.0f);
        duck_entity.facing = Vector2<float>(1.0f, 0.0f);
        duck_entity.mass = 1.0f;
        world.getData().entities.push_back(duck_entity);

        event.state = duck_state;
        spdlog::info("ClockScenario: Starting DUCK event (duration: {}s)", config.duration);
    }

    active_events_[type] = std::move(event);
}

void ClockScenario::updateEvent(World& world, ClockEventType /*type*/, ActiveEvent& event, double deltaTime)
{
    std::visit([&](auto& state) {
        using T = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<T, RainEventState>) {
            updateRainEvent(world, state, deltaTime);
        } else if constexpr (std::is_same_v<T, DuckEventState>) {
            updateDuckEvent(world, state, event.remaining_time);
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

    // Evaporate water in drain at bottom center.
    uint32_t bottomY = world.getData().height - 2;
    uint32_t centerX = world.getData().width / 2;
    constexpr uint32_t DRAIN_SIZE = 5;
    uint32_t halfDrain = DRAIN_SIZE / 2;

    uint32_t drainStart = (centerX > halfDrain) ? centerX - halfDrain : 1;
    uint32_t drainEnd = std::min(centerX + halfDrain, world.getData().width - 2);

    for (uint32_t x = drainStart; x <= drainEnd; ++x) {
        Cell& cell = world.getData().at(x, bottomY);
        if (cell.material_type == MaterialType::WATER) {
            cell.fill_ratio -= 0.5;
            if (cell.fill_ratio < 0.01) {
                cell.replaceMaterial(MaterialType::AIR, 0.0);
            }
        }
    }
}

void ClockScenario::updateDuckEvent(World& world, DuckEventState& state, double remaining_time)
{
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
            world.getData().entities.clear();
            return;
        }
    }

    // Clamp COM.y when grounded.
    if (duck_organism->isOnGround() && duck_com.y > 0.0) {
        duck_com.y = 0.0;
    }

    // Update duck entity to match organism position.
    for (auto& entity : world.getData().entities) {
        if (entity.type == EntityType::DUCK) {
            entity.position = Vector2<float>(
                static_cast<float>(duck_cell.x),
                static_cast<float>(duck_cell.y));
            entity.com = Vector2<float>(
                static_cast<float>(duck_com.x),
                static_cast<float>(duck_com.y));
            entity.facing = duck_organism->getFacing();

            const auto& duck_sparkles = duck_organism->getSparkles();
            entity.sparkles.clear();
            entity.sparkles.reserve(duck_sparkles.size());
            for (const auto& ds : duck_sparkles) {
                SparkleParticle sp;
                sp.position = ds.position;
                sp.opacity = ds.lifetime / ds.max_lifetime;
                entity.sparkles.push_back(sp);
            }
            break;
        }
    }
}

void ClockScenario::endEvent(World& world, ClockEventType type, ActiveEvent& event)
{
    spdlog::info("ClockScenario: Ending {} event",
        type == ClockEventType::RAIN ? "RAIN" : "DUCK");

    if (type == ClockEventType::DUCK) {
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

        world.getData().entities.clear();
    }

    // Set cooldown for this event type.
    const auto& config = DEFAULT_EVENT_CONFIGS.at(type);
    event_cooldowns_[type] = config.cooldown;

    spdlog::info("ClockScenario: Event {} on cooldown for {:.1f}s",
        type == ClockEventType::RAIN ? "RAIN" : "DUCK", config.cooldown);
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
    }

    active_events_.clear();
    event_cooldowns_.clear();
    door_manager_.closeAllDoors(world);
    world.getData().entities.clear();
}

void ClockScenario::evaporateBottomRow(World& world, double deltaTime)
{
    WorldData& data = world.getData();

    // Bottom playable row (height-1 is wall, height-2 is where water sits).
    if (data.height < 2) return;
    uint32_t bottom_y = data.height - 2;

    // Evaporation rate: 50% of fill per second.
    constexpr double EVAPORATION_RATE = 0.5;
    double evaporation_amount = EVAPORATION_RATE * deltaTime;

    // Evaporate water from entire bottom row (excluding wall borders).
    for (uint32_t x = 1; x < data.width - 1; ++x) {
        Cell& cell = data.at(x, bottom_y);
        if (cell.material_type == MaterialType::WATER) {
            cell.fill_ratio -= evaporation_amount;
            if (cell.fill_ratio < 0.01) {
                cell.replaceMaterial(MaterialType::AIR, 0.0);
            }
        }
    }
}

void ClockScenario::materializeWall(World& world, int x, int y)
{
    const WorldData& data = world.getData();

    if (x < 0 || x >= static_cast<int>(data.width) ||
        y < 0 || y >= static_cast<int>(data.height)) {
        return;
    }

    Cell& cell = world.getData().at(x, y);

    if (cell.isEmpty() || cell.material_type == MaterialType::WALL) {
        cell.replaceMaterial(MaterialType::WALL, 1.0);
        return;
    }

    // Find open adjacent cell closest to COM direction.
    Vector2i best_dir{ 0, 0 };
    double best_score = -999.0;

    static constexpr std::array<std::pair<int, int>, 4> directions = {
        { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
    };

    for (auto [dx, dy] : directions) {
        int nx = x + dx;
        int ny = y + dy;

        if (nx < 0 || nx >= static_cast<int>(data.width) ||
            ny < 0 || ny >= static_cast<int>(data.height)) {
            continue;
        }

        const Cell& neighbor = data.at(nx, ny);
        if (!neighbor.isEmpty()) {
            continue;
        }

        // Score by alignment with COM.
        double score = cell.com.x * dx + cell.com.y * dy;
        if (score > best_score) {
            best_score = score;
            best_dir = { dx, dy };
        }
    }

    if (best_dir.x == 0 && best_dir.y == 0) {
        // No open cell available - just overwrite.
        cell.replaceMaterial(MaterialType::WALL, 1.0);
        return;
    }

    // WALL "arrives" from the open cell direction.
    Cell& source = world.getData().at(x + best_dir.x, y + best_dir.y);
    source.replaceMaterial(MaterialType::WALL, 1.0);

    // Create MaterialMove for WALL moving into the cell.
    MaterialMove move;
    move.fromX = x + best_dir.x;
    move.fromY = y + best_dir.y;
    move.toX = x;
    move.toY = y;
    move.material = MaterialType::WALL;
    move.momentum = Vector2d(-best_dir.x, -best_dir.y) * 2.0;
    move.collision_energy = 100.0;
    move.boundary_normal = Vector2d(-best_dir.x, -best_dir.y);
    move.material_mass = 1000.0;
    move.target_mass = cell.fill_ratio * getMaterialDensity(cell.material_type);

    // Execute swap: WALL moves in, displaced material moves out.
    Vector2i swap_dir = { -best_dir.x, -best_dir.y };
    world.getCollisionCalculator().swapCounterMovingMaterials(source, cell, swap_dir, move);
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
            materializeWall(world, x, 0);
        }
        if (!door_manager_.isOpenDoor(bottom_pos)) {
            materializeWall(world, x, height - 1);
        }
    }

    // Left and right borders.
    for (uint32_t y = 0; y < height; ++y) {
        Vector2i left_pos{ 0, static_cast<int>(y) };
        Vector2i right_pos{ static_cast<int>(width - 1), static_cast<int>(y) };

        if (!door_manager_.isOpenDoor(left_pos)) {
            materializeWall(world, 0, y);
        }
        if (!door_manager_.isOpenDoor(right_pos)) {
            materializeWall(world, width - 1, y);
        }
    }

    // Ensure roof cells are walls.
    for (const auto& roof_pos : door_manager_.getRoofPositions()) {
        materializeWall(world, roof_pos.x, roof_pos.y);
    }
}

} // namespace DirtSim
