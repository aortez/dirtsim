#include "ClockScenario.h"
#include "core/Cell.h"
#include "core/Entity.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
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
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_WIDTH;
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_WIDTH;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_WIDTH;
    case ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_WIDTH;
    }
    return ClockFonts::SEGMENT7_WIDTH;
}

int ClockScenario::getDigitHeight() const
{
    switch (config_.font) {
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_HEIGHT;
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_HEIGHT;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_HEIGHT;
    case ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_HEIGHT;
    }
    return ClockFonts::SEGMENT7_HEIGHT;
}

int ClockScenario::getDigitGap() const
{
    switch (config_.font) {
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_GAP;
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_GAP;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_GAP;
    case ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_GAP;
    }
    return ClockFonts::SEGMENT7_GAP;
}

int ClockScenario::getColonWidth() const
{
    switch (config_.font) {
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_COLON_WIDTH;
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_COLON_WIDTH;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_COLON_WIDTH;
    case ClockFont::Segment7Tall:
        return ClockFonts::SEGMENT7_TALL_COLON_WIDTH;
    }
    return ClockFonts::SEGMENT7_COLON_WIDTH;
}

int ClockScenario::getColonPadding() const
{
    switch (config_.font) {
    case ClockFont::DotMatrix:
        return ClockFonts::DOT_MATRIX_COLON_PADDING;
    case ClockFont::Segment7:
        return ClockFonts::SEGMENT7_COLON_PADDING;
    case ClockFont::Segment7Large:
        return ClockFonts::SEGMENT7_LARGE_COLON_PADDING;
    case ClockFont::Segment7Tall:
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

        // Recalculate and reset if dimensions changed (including font).
        if (needs_resize) {
            recalculateDimensions();

            spdlog::info(
                "ClockScenario: Resetting world to {}x{} (font={}, show_seconds={}, display={}x{})",
                metadata_.requiredWidth,
                metadata_.requiredHeight,
                static_cast<int>(config_.font),
                config_.show_seconds,
                config_.target_display_width,
                config_.target_display_height);

            // Cancel any active event before resizing.
            cancelEvent(world);

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
        world.getData().at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
        world.getData().at(x, height - 1).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Left and right borders.
    for (uint32_t y = 0; y < height; ++y) {
        world.getData().at(0, y).replaceMaterial(MaterialType::WALL, 1.0);
        world.getData().at(width - 1, y).replaceMaterial(MaterialType::WALL, 1.0);
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

    // Evaporate water from bottom row (10% per second).
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
            case ClockFont::DotMatrix:
                pixel = ClockFonts::DOT_MATRIX_PATTERNS[digit][row][col];
                break;
            case ClockFont::Segment7:
                pixel = ClockFonts::SEGMENT7_PATTERNS[digit][row][col];
                break;
            case ClockFont::Segment7Large:
                pixel = ClockFonts::SEGMENT7_LARGE_PATTERNS[digit][row][col];
                break;
            case ClockFont::Segment7Tall:
                pixel = ClockFonts::SEGMENT7_TALL_PATTERNS[digit][row][col];
                break;
            }

            if (pixel) {
                world.getData().at(x, y).replaceMaterial(MaterialType::WALL, 1.0);
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
        int dot_height = (config_.font == ClockFont::Segment7Large) ? 2 : 1;

        for (int dy = 0; dy < dot_height; ++dy) {
            int y1 = dot1_y + dy;
            int y2 = dot2_y + dy;

            if (y1 >= 0 && y1 < static_cast<int>(world.getData().height)) {
                world.getData().at(x, y1).replaceMaterial(MaterialType::WALL, 1.0);
                painted_cells_.push_back(Vector2i{ x, y1 });
            }
            if (y2 >= 0 && y2 < static_cast<int>(world.getData().height)) {
                world.getData().at(x, y2).replaceMaterial(MaterialType::WALL, 1.0);
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

// Event system constants.
static constexpr double BASE_EVENT_DELAY = 30.0; // Base delay between events (seconds).
static constexpr double RAIN_DURATION = 10.0;    // Rain event duration (seconds).
static constexpr double DUCK_SPEED = 8.0;        // Duck speed (cells per second).

void ClockScenario::updateEvents(World& world, double deltaTime)
{
    // Events disabled if frequency is 0.
    if (config_.event_frequency <= 0.0) {
        return;
    }

    time_since_init_ += deltaTime;

    if (current_event_ == EventType::NONE) {
        // No event active - check if we should start one.
        if (!first_event_triggered_) {
            // First event triggers immediately.
            first_event_triggered_ = true;
            // Random event: 50% DUCK, 50% RAIN.
            EventType event = (uniform_dist_(rng_) < 0.8) ? EventType::DUCK : EventType::RAIN;
            startEvent(world, event);
        }
        else {
            // Wait for timer to expire.
            event_timer_ -= deltaTime;
            if (event_timer_ <= 0.0) {
                // Time to start next event.
                // Random event: 50% DUCK, 50% RAIN.
                EventType event = (uniform_dist_(rng_) < 0.5) ? EventType::DUCK : EventType::RAIN;
                startEvent(world, event);
            }
        }
    }
    else {
        // Event is active - update it.
        if (current_event_ == EventType::RAIN) {
            updateRainEvent(world, deltaTime);
        }
        else if (current_event_ == EventType::DUCK) {
            updateDuckEvent(world);
        }

        // Check if event should end.
        event_timer_ -= deltaTime;
        if (event_timer_ <= 0.0) {
            endEvent(world);
        }
    }
}

void ClockScenario::startEvent(World& world, EventType type)
{
    current_event_ = type;

    if (type == EventType::RAIN) {
        event_timer_ = RAIN_DURATION;
        spdlog::info("ClockScenario: Starting RAIN event (duration: {}s)", RAIN_DURATION);
    }
    else if (type == EventType::DUCK) {
        // Duck event duration (30 seconds).
        event_timer_ = 30.0;

        // Use WallBouncingBrain with jumping enabled (temporarily always, will be 50/50 later).
        std::unique_ptr<DuckBrain> brain = std::make_unique<WallBouncingBrain>(true);
        spdlog::info("ClockScenario: Creating duck with WallBouncingBrain (jumping enabled)");

        // Choose random entrance side and calculate door position.
        entrance_side_ = (uniform_dist_(rng_) < 0.5) ? DoorSide::LEFT : DoorSide::RIGHT;
        uint32_t door_y = world.getData().height - 2; // Floor level (above bottom wall).
        uint32_t door_x = (entrance_side_ == DoorSide::LEFT) ? 0 : world.getData().width - 1;

        entrance_door_pos_ = Vector2i{ static_cast<int>(door_x), static_cast<int>(door_y) };
        entrance_door_open_ = true;
        exit_door_open_ = false;

        // Calculate exit door position (opposite side).
        uint32_t exit_x = (entrance_side_ == DoorSide::LEFT) ? world.getData().width - 1 : 0;
        exit_door_pos_ = Vector2i{ static_cast<int>(exit_x), static_cast<int>(door_y) };

        // Open entrance door (remove wall cell).
        world.getData().at(door_x, door_y) = Cell();

        // Spawn duck just inside the door (not at the wall column itself).
        // Left door: spawn at x=1, Right door: spawn at x=width-2.
        // TODO: Ideally spawn at door_x and fix physics at world boundary.
        uint32_t duck_x = (entrance_side_ == DoorSide::LEFT) ? 1 : world.getData().width - 2;
        uint32_t duck_y = door_y;
        duck_organism_id_ = world.getOrganismManager().createDuck(world, duck_x, duck_y, std::move(brain));

        spdlog::info("ClockScenario: Duck organism {} enters through {} door at ({}, {})",
            duck_organism_id_,
            entrance_side_ == DoorSide::LEFT ? "LEFT" : "RIGHT",
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

        spdlog::info("ClockScenario: Starting DUCK event (duration: 30s)");
    }
}

void ClockScenario::updateRainEvent(World& world, double deltaTime)
{
    // Spawn water drops at random X positions near the top.
    constexpr double DROPS_PER_SECOND = 10.0;
    double drop_probability = DROPS_PER_SECOND * deltaTime;

    if (uniform_dist_(rng_) < drop_probability) {
        std::uniform_int_distribution<uint32_t> x_dist(2, world.getData().width - 3);
        uint32_t x = x_dist(rng_);
        uint32_t y = 2; // Near top (below wall border).

        world.addMaterialAtCell(x, y, MaterialType::WATER, 0.5);
    }

    // Evaporate water in drain at bottom center.
    uint32_t bottomY = world.getData().height - 2; // Above wall border.
    uint32_t centerX = world.getData().width / 2;
    constexpr uint32_t DRAIN_SIZE = 5;
    uint32_t halfDrain = DRAIN_SIZE / 2;

    uint32_t drainStart = (centerX > halfDrain) ? centerX - halfDrain : 1;
    uint32_t drainEnd = std::min(centerX + halfDrain, world.getData().width - 2);

    for (uint32_t x = drainStart; x <= drainEnd; ++x) {
        Cell& cell = world.getData().at(x, bottomY);
        if (cell.material_type == MaterialType::WATER) {
            // Evaporate water quickly (50% per tick).
            cell.fill_ratio -= 0.5;
            if (cell.fill_ratio < 0.01) {
                cell.replaceMaterial(MaterialType::AIR, 0.0);
            }
        }
    }
}

void ClockScenario::updateDuckEvent(World& world)
{
    // Get duck organism.
    Duck* duck_organism = world.getOrganismManager().getDuck(duck_organism_id_);
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
    if (entrance_door_open_ && duck_cell != entrance_door_pos_) {
        world.getData().at(entrance_door_pos_.x, entrance_door_pos_.y)
            .replaceMaterial(MaterialType::WALL, 1.0);
        entrance_door_open_ = false;
        spdlog::info("ClockScenario: Entrance door closed at ({}, {})",
            entrance_door_pos_.x, entrance_door_pos_.y);
    }

    // Open exit door in the last 7 seconds.
    if (!exit_door_open_ && event_timer_ <= 7.0) {
        world.getData().at(exit_door_pos_.x, exit_door_pos_.y) = Cell();
        exit_door_open_ = true;
        spdlog::info("ClockScenario: Exit door opened at ({}, {})",
            exit_door_pos_.x, exit_door_pos_.y);
    }

    // Check if duck entered the exit door and passed the middle of the cell.
    // Exit is on opposite side of entrance, so check COM direction:
    // - Entrance LEFT means exit RIGHT: duck moving right, trigger when COM.x > 0.
    // - Entrance RIGHT means exit LEFT: duck moving left, trigger when COM.x < 0.
    if (exit_door_open_ && duck_cell == exit_door_pos_) {
        bool past_middle = (entrance_side_ == DoorSide::LEFT) ? (duck_com.x > 0.0) : (duck_com.x < 0.0);
        if (past_middle) {
            spdlog::info("ClockScenario: Duck exited through door at ({}, {}), COM.x={:.2f}",
                exit_door_pos_.x, exit_door_pos_.y, duck_com.x);

            // Remove the duck immediately so it disappears into the door.
            world.getOrganismManager().removeOrganismFromWorld(world, duck_organism_id_);
            duck_organism_id_ = INVALID_ORGANISM_ID;
            world.getData().entities.clear();

            // Set timer to 1 second so the door stays open briefly, then closes.
            if (event_timer_ > 1.0) {
                event_timer_ = 1.0;
            }
            return; // Duck is gone, nothing more to update.
        }
    }

    // If duck is on ground, clamp COM.y to prevent sinking into floor.
    // COM.y in range [-1, 1], where +1 = bottom of cell.
    // Set to 0.0 (center) when grounded and COM is positive (bottom half).
    if (duck_organism->isOnGround() && duck_com.y > 0.0) {
        duck_com.y = 0.0;
    }

    // Find and update the duck entity to match organism position.
    for (auto& entity : world.getData().entities) {
        if (entity.type == EntityType::DUCK) {
            // Sync entity position and COM with organism's cell.
            entity.position = Vector2<float>(
                static_cast<float>(duck_cell.x),
                static_cast<float>(duck_cell.y));
            entity.com = Vector2<float>(
                static_cast<float>(duck_com.x),
                static_cast<float>(duck_com.y));
            entity.facing = duck_organism->getFacing();

            // Copy sparkles from organism to entity for rendering.
            const auto& duck_sparkles = duck_organism->getSparkles();
            entity.sparkles.clear();
            entity.sparkles.reserve(duck_sparkles.size());
            for (const auto& ds : duck_sparkles) {
                SparkleParticle sp;
                sp.position = ds.position;
                sp.opacity = ds.lifetime / ds.max_lifetime;  // Fade based on remaining lifetime.
                entity.sparkles.push_back(sp);
            }
            break;
        }
    }
}

void ClockScenario::endEvent(World& world)
{
    spdlog::info("ClockScenario: Ending {} event", current_event_ == EventType::RAIN ? "RAIN" : "DUCK");

    // Clean up event-specific state.
    if (current_event_ == EventType::DUCK) {
        if (duck_organism_id_ != INVALID_ORGANISM_ID) {
            world.getOrganismManager().removeOrganismFromWorld(world, duck_organism_id_);
            duck_organism_id_ = INVALID_ORGANISM_ID;
        }

        // Close any open doors.
        if (entrance_door_open_) {
            world.getData().at(entrance_door_pos_.x, entrance_door_pos_.y)
                .replaceMaterial(MaterialType::WALL, 1.0);
            entrance_door_open_ = false;
            spdlog::info("ClockScenario: Entrance door closed at end of event");
        }
        if (exit_door_open_) {
            world.getData().at(exit_door_pos_.x, exit_door_pos_.y)
                .replaceMaterial(MaterialType::WALL, 1.0);
            exit_door_open_ = false;
            spdlog::info("ClockScenario: Exit door closed at end of event");
        }

        // Remove duck and sparkle entities.
        world.getData().entities.clear();
    }

    // Schedule next event.
    double delay = BASE_EVENT_DELAY * (1.0 - config_.event_frequency);
    // Add random jitter (±20%).
    double jitter = (uniform_dist_(rng_) * 0.4 - 0.2) * delay;
    event_timer_ = delay + jitter;

    current_event_ = EventType::NONE;

    spdlog::info("ClockScenario: Next event in {:.1f}s", event_timer_);
}

void ClockScenario::cancelEvent(World& world)
{
    if (current_event_ == EventType::NONE) {
        return;
    }

    spdlog::info("ClockScenario: Canceling {} event due to resize",
        current_event_ == EventType::RAIN ? "RAIN" : "DUCK");

    // Clean up event-specific state.
    if (current_event_ == EventType::DUCK) {
        if (duck_organism_id_ != INVALID_ORGANISM_ID) {
            world.getOrganismManager().removeOrganismFromWorld(world, duck_organism_id_);
            duck_organism_id_ = INVALID_ORGANISM_ID;
        }
        world.getData().entities.clear();
    }

    // Reset all event state.
    current_event_ = EventType::NONE;
    event_timer_ = 0.0;
    first_event_triggered_ = false;
    entrance_door_open_ = false;
    exit_door_open_ = false;
    entrance_door_pos_ = Vector2i{ -1, -1 };
    exit_door_pos_ = Vector2i{ -1, -1 };
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

} // namespace DirtSim
