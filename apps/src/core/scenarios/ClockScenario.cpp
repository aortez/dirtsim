#include "ClockScenario.h"
#include "clock_scenario/CharacterMetrics.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/ColorNames.h"
#include "core/FragmentationParams.h"
#include "core/LightManager.h"
#include "core/LightTypes.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldCollisionCalculator.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/WorldLightCalculator.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/OrganismManager.h"
#include "spdlog/spdlog.h"

#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <lvgl.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DirtSim {

namespace {

uint32_t getMaterialColor(Material::EnumType mat)
{
    switch (mat) {
        case Material::EnumType::Air:
            return ColorNames::black();
        case Material::EnumType::Dirt:
            return ColorNames::dirt();
        case Material::EnumType::Leaf:
            return ColorNames::leaf();
        case Material::EnumType::Metal:
            return ColorNames::metal();
        case Material::EnumType::Root:
            return ColorNames::root();
        case Material::EnumType::Sand:
            return ColorNames::sand();
        case Material::EnumType::Seed:
            return ColorNames::seed();
        case Material::EnumType::Wall:
            return ColorNames::stone();
        case Material::EnumType::Water:
            return ColorNames::water();
        case Material::EnumType::Wood:
            return ColorNames::wood();
    }
    return ColorNames::white();
}

} // namespace

ClockScenario::ClockScenario(ClockEventConfigs event_configs)
    : event_configs_(std::move(event_configs))
{
    metadata_.name = "Clock";
    metadata_.description = "Digital clock displaying system time (HH:MM:SS)";
    metadata_.category = "demo";

    recalculateDimensions();
}

ClockScenario::~ClockScenario() = default;

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
        case ClockEventType::DIGIT_SLIDE:
            return event_configs_.digit_slide.timing;
        case ClockEventType::DUCK:
            return event_configs_.duck.timing;
        case ClockEventType::MARQUEE:
            return event_configs_.marquee.timing;
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
        case Config::ClockFont::Montserrat24:
            return ClockFonts::MONTSERRAT24_WIDTH;
        case Config::ClockFont::NotoColorEmoji:
            return ClockFonts::NOTO_EMOJI_WIDTH;
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
        case Config::ClockFont::Montserrat24:
            return ClockFonts::MONTSERRAT24_HEIGHT;
        case Config::ClockFont::NotoColorEmoji:
            return ClockFonts::NOTO_EMOJI_HEIGHT;
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
        case Config::ClockFont::Montserrat24:
            return ClockFonts::MONTSERRAT24_GAP;
        case Config::ClockFont::NotoColorEmoji:
            return ClockFonts::NOTO_EMOJI_GAP;
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
        case Config::ClockFont::Montserrat24:
            return ClockFonts::MONTSERRAT24_COLON_WIDTH;
        case Config::ClockFont::NotoColorEmoji:
            return ClockFonts::NOTO_EMOJI_COLON_WIDTH;
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
        case Config::ClockFont::Montserrat24:
            return ClockFonts::MONTSERRAT24_COLON_PADDING;
        case Config::ClockFont::NotoColorEmoji:
            return ClockFonts::NOTO_EMOJI_COLON_PADDING;
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

void ClockScenario::ensureFontSamplerInitialized() const
{
    // Check if we need to create or recreate the sampler for a different font.
    bool needs_sampler_fonts =
        (config_.font == Config::ClockFont::Montserrat24
         || config_.font == Config::ClockFont::NotoColorEmoji);

    if (!needs_sampler_fonts) {
        // This font uses static patterns, not FontSampler.
        return;
    }

    // Recreate sampler if font changed.
    if (font_sampler_ && font_sampler_font_ != config_.font) {
        font_sampler_.reset();
    }

    if (font_sampler_) {
        return;
    }

    // FontSampler::initCanvas() handles LVGL initialization and headless display creation.
    // Do not create a display here - FontSampler's ensureHeadlessDisplay() properly
    // calls lv_init() before creating the display.

    if (config_.font == Config::ClockFont::NotoColorEmoji) {
        // Create FontSampler with NotoColorEmoji via FreeType.
        // Path relative to executable (fonts/ directory).
        font_sampler_ = std::make_unique<FontSampler>(
            "fonts/NotoColorEmoji.ttf",
            ClockFonts::NOTO_EMOJI_HEIGHT,    // Font size matches target height.
            ClockFonts::NOTO_EMOJI_WIDTH + 4, // Canvas slightly larger than glyph.
            ClockFonts::NOTO_EMOJI_HEIGHT + 4,
            0.3f);

        spdlog::info("ClockScenario: FontSampler initialized for NotoColorEmoji");
    }
    else {
        // Create FontSampler with Montserrat 24pt.
        // Canvas size starts large to avoid resize iterations. Trimmed patterns will
        // auto-resize if clipping is detected, then trim whitespace for a tight fit.
        font_sampler_ = std::make_unique<FontSampler>(
            &lv_font_montserrat_24,
            48, // Large initial canvas to fit 24pt glyphs without resizing.
            48,
            0.3f);

        spdlog::info("ClockScenario: FontSampler initialized for Montserrat 24pt");
    }

    font_sampler_font_ = config_.font;

    // Precache digits 0-9 using trimmed patterns.
    for (char c = '0'; c <= '9'; ++c) {
        font_sampler_->getCachedPatternTrimmed(c);
    }
}

const std::vector<std::vector<bool>>& ClockScenario::getSampledDigitPattern(int digit) const
{
    ensureFontSamplerInitialized();

    char c = static_cast<char>('0' + digit);
    return font_sampler_->getCachedPatternTrimmed(c);
}

const CharacterMetrics& ClockScenario::getMetrics() const
{
    // Recreate metrics if font changed.
    if (metrics_cache_ && metrics_cache_font_ != config_.font) {
        metrics_cache_.reset();
    }

    if (!metrics_cache_) {
        metrics_cache_ = std::make_unique<CharacterMetrics>(config_.font);
        metrics_cache_font_ = config_.font;
    }

    return *metrics_cache_;
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
        double display_aspect =
            static_cast<double>(config_.targetDisplayWidth) / config_.targetDisplayHeight;

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
        bool layout_changed =
            (incoming.showSeconds != config_.showSeconds) || (incoming.font != config_.font);

        // Check if only dimensions changed (can handle incrementally).
        bool dimensions_changed = (incoming.autoScale != config_.autoScale)
            || (incoming.targetDisplayWidth != config_.targetDisplayWidth)
            || (incoming.targetDisplayHeight != config_.targetDisplayHeight)
            || (incoming.marginPixels != config_.marginPixels);

        // Track event toggle changes before updating config_.
        bool color_cycle_was_enabled = config_.colorCycleEnabled;
        bool color_showcase_was_enabled = config_.colorShowcaseEnabled;
        bool digit_slide_was_enabled = config_.digitSlideEnabled;
        bool duck_was_enabled = config_.duckEnabled;
        bool marquee_was_enabled = config_.marqueeEnabled;
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
            for (int y = 1; y < data.height - 1; ++y) {
                for (int x = 1; x < data.width - 1; ++x) {
                    if (data.at(x, y).material_type == Material::EnumType::Wall) {
                        data.at(x, y) = Cell();
                    }
                }
            }

            // Redraw walls at new boundaries and digits at new centered positions.
            redrawWalls(world);
            drawTime(world);
        }

        // Handle manual event toggling.
        // Skip event triggering during initial config load (before first tick).
        // Events enabled in config will trigger naturally via tryTriggerPeriodicEvents
        // or tryTriggerTimeChangeEvents after the simulation starts.
        if (first_tick_done_) {
            // Color cycle event toggle.
            if (config_.colorCycleEnabled && !color_cycle_was_enabled) {
                // User enabled color cycle - start it if not already active.
                if (!active_events_.contains(ClockEventType::COLOR_CYCLE)) {
                    event_cooldowns_[ClockEventType::COLOR_CYCLE] = 0.0; // Clear cooldown.
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
                    event_cooldowns_[ClockEventType::COLOR_CYCLE] =
                        0.0; // No cooldown for manual stop.
                    spdlog::info("ClockScenario: Color cycle manually disabled");
                }
            }

            // Color showcase event toggle.
            if (config_.colorShowcaseEnabled && !color_showcase_was_enabled) {
                // User enabled color showcase - start it if not already active.
                if (!active_events_.contains(ClockEventType::COLOR_SHOWCASE)) {
                    event_cooldowns_[ClockEventType::COLOR_SHOWCASE] = 0.0; // Clear cooldown.
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
                    event_cooldowns_[ClockEventType::COLOR_SHOWCASE] =
                        0.0; // No cooldown for manual stop.
                    spdlog::info("ClockScenario: Color showcase manually disabled");
                }
            }

            // Rain event toggle.
            if (config_.rainEnabled && !rain_was_enabled) {
                // User enabled rain - start it if not already active.
                if (!active_events_.contains(ClockEventType::RAIN)) {
                    event_cooldowns_[ClockEventType::RAIN] = 0.0; // Clear cooldown.
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
                    event_cooldowns_[ClockEventType::RAIN] = 0.0; // No cooldown for manual stop.
                    spdlog::info("ClockScenario: Rain manually disabled");
                }
            }

            // Digit slide event toggle.
            if (config_.digitSlideEnabled && !digit_slide_was_enabled) {
                // User enabled digit slide - start it if not already active.
                if (!active_events_.contains(ClockEventType::DIGIT_SLIDE)) {
                    event_cooldowns_[ClockEventType::DIGIT_SLIDE] = 0.0; // Clear cooldown.
                    startEvent(world, ClockEventType::DIGIT_SLIDE);
                    spdlog::info("ClockScenario: Digit slide manually enabled");
                }
            }
            else if (!config_.digitSlideEnabled && digit_slide_was_enabled) {
                // User disabled digit slide - stop it if active.
                auto it = active_events_.find(ClockEventType::DIGIT_SLIDE);
                if (it != active_events_.end()) {
                    endEvent(world, ClockEventType::DIGIT_SLIDE, it->second);
                    active_events_.erase(it);
                    event_cooldowns_[ClockEventType::DIGIT_SLIDE] =
                        0.0; // No cooldown for manual stop.
                    spdlog::info("ClockScenario: Digit slide manually disabled");
                }
            }

            // Duck event toggle.
            if (config_.duckEnabled && !duck_was_enabled) {
                // User enabled duck - start it if not already active.
                if (!active_events_.contains(ClockEventType::DUCK)) {
                    event_cooldowns_[ClockEventType::DUCK] = 0.0; // Clear cooldown.
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
                    event_cooldowns_[ClockEventType::DUCK] = 0.0; // No cooldown for manual stop.
                    spdlog::info("ClockScenario: Duck manually disabled");
                }
            }

            // Marquee event toggle.
            if (config_.marqueeEnabled && !marquee_was_enabled) {
                // User enabled marquee - start it if not already active.
                if (!active_events_.contains(ClockEventType::MARQUEE)) {
                    event_cooldowns_[ClockEventType::MARQUEE] = 0.0; // Clear cooldown.
                    startEvent(world, ClockEventType::MARQUEE);
                    spdlog::info("ClockScenario: Marquee manually enabled");
                }
            }
            else if (!config_.marqueeEnabled && marquee_was_enabled) {
                // User disabled marquee - stop it if active.
                auto it = active_events_.find(ClockEventType::MARQUEE);
                if (it != active_events_.end()) {
                    endEvent(world, ClockEventType::MARQUEE, it->second);
                    active_events_.erase(it);
                    event_cooldowns_[ClockEventType::MARQUEE] = 0.0; // No cooldown for manual stop.
                    spdlog::info("ClockScenario: Marquee manually disabled");
                }
            }

            // Meltdown event trigger (one-shot).
            if (config_.meltdownEnabled) {
                // User triggered meltdown - start it if not already active.
                if (!active_events_.contains(ClockEventType::MELTDOWN)) {
                    event_cooldowns_[ClockEventType::MELTDOWN] = 0.0; // Clear cooldown.
                    startEvent(world, ClockEventType::MELTDOWN);
                    spdlog::info("ClockScenario: Meltdown manually triggered");
                }
                // Reset the trigger flag immediately.
                config_.meltdownEnabled = false;
            }
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

    // Dark lighting - emissive digits glow against dim background.
    auto& light = world.getPhysicsSettings().light;
    light.sun_intensity = 0.1f;
    light.ambient_intensity = 0.0f;

    // Clear world to empty state.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }

    // Draw walls using centralized wall system.
    redrawWalls(world);

    // Draw initial time (emissive will be set on first tick).
    drawTime(world);

    // Add static torch lights at corners.
    world.getLightManager().clear();
    const WorldData& data = world.getData();
    world.getLightManager().addLight(PointLight{
        .position = Vector2d{ static_cast<double>(data.width - 2), static_cast<double>(2) },
        .color = ColorNames::torchOrange(),
        .intensity = 0.1f,
        .radius = 15.0f,
        .attenuation = 0.05f });

    world.getLightManager().addLight(
        PointLight{ .position = Vector2d{ static_cast<double>(2), static_cast<double>(2) },
                    .color = ColorNames::torchOrange(),
                    .intensity = 0.1f,
                    .radius = 15.0f,
                    .attenuation = 0.05f });

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
    first_tick_done_ = true;
    redrawWalls(world);

    // Update event system first so digit slide can detect time changes
    // before drawTime() updates last_drawn_time_.
    updateEvents(world, deltaTime);

    // Process scheduled door removals.
    door_manager_.update();

    // Check if digit slide is animating (takes over rendering like marquee).
    bool digit_slide_animating = false;
    if (isEventActive(ClockEventType::DIGIT_SLIDE)) {
        auto it = active_events_.find(ClockEventType::DIGIT_SLIDE);
        if (it != active_events_.end()) {
            const auto& state = std::get<DigitSlideEventState>(it->second.state);
            digit_slide_animating = state.slide_state.active;
        }
    }

    if (!isMeltdownActive() && !isEventActive(ClockEventType::MARQUEE) && !digit_slide_animating) {
        drawTime(world);
    }

    // Manage floor drain based on water level.
    // Runs after events so drain clearing catches any floor restored by obstacle clearing.
    updateDrain(world, deltaTime);

    // Debug check: verify all WOOD cells have an associated organism.
    // WOOD cells only come from ducks in this scenario, so orphaned WOOD is a bug.
    const WorldData& data = world.getData();
    const auto& org_grid = world.getOrganismManager().getGrid();
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            size_t idx = y * data.width + x;
            if (data.cells[idx].material_type == Material::EnumType::Wood) {
                if (org_grid[idx] == INVALID_ORGANISM_ID) {
                    spdlog::error(
                        "ClockScenario: Orphaned WOOD cell at ({}, {}) with no organism!", x, y);
                    DIRTSIM_ASSERT(false, "Orphaned WOOD cell found - see log for details");
                }
            }
        }
    }
}

int ClockScenario::calculateTotalWidth() const
{
    // Calculate width based on the time string format.
    // Format: "H H : M M" or "H H : M M : S S"
    // - Digits contribute digit width.
    // - Colons contribute colon width.
    // - Spaces contribute digit gap.
    int dw = getDigitWidth();
    int dg = getDigitGap();
    int cw = getColonWidth();

    if (config_.showSeconds) {
        // "H H : M M : S S" = 6 digits, 2 colons, 7 spaces.
        return 6 * dw + 2 * cw + 7 * dg;
    }
    else {
        // "H H : M M" = 4 digits, 1 colon, 4 spaces.
        return 4 * dw + 1 * cw + 4 * dg;
    }
}

void ClockScenario::clearDigits(World& world)
{
    WorldData& data = world.getData();

    // Clear interior WALL cells (digit cells) but NOT:
    // - Boundary cells (x=0, x=width-1, y=0, y=height-1).
    // - Door roof cells.
    // - Hurdle obstacle cells.
    for (int y = 1; y < data.height - 1; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type != Material::EnumType::Wall) {
                continue;
            }

            // Skip door roof cells.
            Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };
            if (door_manager_.isRoofCellAt(pos, data)) {
                continue;
            }

            // Skip hurdle obstacle cells (one row above floor).
            if (y == data.height - 2 && obstacle_manager_.isHurdleAt(x)) {
                continue;
            }

            // This is a digit cell - clear it.
            cell = Cell();
        }
    }
}

void ClockScenario::drawCharacter(
    World& world, const std::string& utf8Char, int start_x, int start_y)
{
    if (utf8Char.empty() || utf8Char == " ") {
        return;
    }

    // Color fonts use material grid rendering for all characters.
    if (config_.font == Config::ClockFont::NotoColorEmoji) {
        drawCharacterWithMaterials(world, utf8Char, start_x, start_y);
        return;
    }

    // Binary fonts use pattern-based rendering.
    drawCharacterBinary(world, utf8Char, start_x, start_y);
}

void ClockScenario::drawCharacterBinary(
    World& world, const std::string& utf8Char, int start_x, int start_y)
{
    if (utf8Char.empty() || utf8Char == " ") {
        return;
    }

    // Determine character dimensions based on type.
    int width = (utf8Char == ":") ? getColonWidth() : getDigitWidth();
    int height = getDigitHeight();

    // Render character using pixel patterns.
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int x = start_x + col;
            int y = start_y + row;

            if (x < 0 || x >= world.getData().width || y < 0 || y >= world.getData().height) {
                continue;
            }

            if (getCharacterPixel(utf8Char, row, col)) {
                placeDigitPixel(world, x, y, config_.digitMaterial);
            }
        }
    }
}

bool ClockScenario::getCharacterPixel(const std::string& utf8Char, int row, int col) const
{
    // Handle colon - two dots at 1/3 and 2/3 height.
    if (utf8Char == ":") {
        int dh = getDigitHeight();
        int cw = getColonWidth();

        // Check if column is within colon width.
        if (col >= cw) {
            return false;
        }

        // Calculate dot positions.
        int dot1_row = dh / 3;
        int dot2_row = (2 * dh) / 3;

        // For large font, draw 2x2 dots; otherwise single pixels.
        int dot_height = (config_.font == Config::ClockFont::Segment7Large) ? 2 : 1;

        // Check if row matches either dot position.
        bool is_dot1 = (row >= dot1_row && row < dot1_row + dot_height);
        bool is_dot2 = (row >= dot2_row && row < dot2_row + dot_height);

        return is_dot1 || is_dot2;
    }

    // Handle digits 0-9.
    if (utf8Char.size() == 1 && utf8Char[0] >= '0' && utf8Char[0] <= '9') {
        int digit = utf8Char[0] - '0';

        switch (config_.font) {
            case Config::ClockFont::DotMatrix:
                return ClockFonts::DOT_MATRIX_PATTERNS[digit][row][col];

            case Config::ClockFont::Montserrat24: {
                const auto& pattern = getSampledDigitPattern(digit);
                if (row < static_cast<int>(pattern.size())
                    && col < static_cast<int>(pattern[row].size())) {
                    return pattern[row][col];
                }
                return false;
            }

            case Config::ClockFont::NotoColorEmoji:
                // Color fonts don't use binary pixel lookup.
                return false;

            case Config::ClockFont::Segment7:
                return ClockFonts::SEGMENT7_PATTERNS[digit][row][col];

            case Config::ClockFont::Segment7ExtraTall:
                return ClockFonts::SEGMENT7_EXTRA_TALL_PATTERNS[digit][row][col];

            case Config::ClockFont::Segment7Jumbo:
                return ClockFonts::SEGMENT7_JUMBO_PATTERNS[digit][row][col];

            case Config::ClockFont::Segment7Large:
                return ClockFonts::SEGMENT7_LARGE_PATTERNS[digit][row][col];

            case Config::ClockFont::Segment7Tall:
                return ClockFonts::SEGMENT7_TALL_PATTERNS[digit][row][col];
        }
    }

    // Unknown character - no pixel.
    return false;
}

void ClockScenario::drawCharacterWithMaterials(
    World& world, const std::string& utf8Char, int start_x, int start_y)
{
    ensureFontSamplerInitialized();

    int dw = getDigitWidth();
    int dh = getDigitHeight();

    // Sample and downsample the character to digit size.
    auto materialGrid = font_sampler_->sampleAndDownsample(utf8Char, dw, dh, 0.5f);

    if (materialGrid.width == 0 || materialGrid.height == 0) {
        spdlog::warn("ClockScenario: Failed to sample character '{}'", utf8Char);
        return;
    }

    for (int row = 0; row < materialGrid.height; ++row) {
        for (int col = 0; col < materialGrid.width; ++col) {
            int x = start_x + col;
            int y = start_y + row;

            // Bounds check.
            if (x < 0 || x >= world.getData().width || y < 0 || y >= world.getData().height) {
                continue;
            }

            Material::EnumType mat = materialGrid.at(col, row);

            // Skip AIR - leave background unchanged.
            if (mat == Material::EnumType::Air) {
                continue;
            }

            placeDigitPixel(world, x, y, mat);
        }
    }
}

void ClockScenario::placeDigitPixel(World& world, int x, int y, Material::EnumType renderMaterial)
{
    // Use WALL (immobile) but render as the specified material.
    world.replaceMaterialAtCell(
        { static_cast<int16_t>(x), static_cast<int16_t>(y) }, Material::EnumType::Wall);
    world.getData().at(x, y).render_as = static_cast<int8_t>(renderMaterial);

    // Make digit cells emissive so they glow in darkness.
    uint32_t color = getMaterialColor(renderMaterial);
    float intensity = static_cast<float>(config_.digitEmissiveness);
    world.getLightCalculator().setEmissive(x, y, color, intensity);
}

void ClockScenario::drawTimeString(World& world, const std::string& time_str)
{
    // Clear previous digits.
    clearDigits(world);

    // Use layoutString for proper UTF-8 handling and positioning.
    const CharacterMetrics& metrics = getMetrics();
    auto getWidth = metrics.widthFunction();

    int total_width = calculateStringWidth(time_str, getWidth);
    int dh = metrics.getHeight();
    int start_x = (world.getData().width - total_width) / 2;
    int start_y = (world.getData().height - dh) / 2;

    // Layout and draw characters.
    auto placements = layoutString(time_str, getWidth);
    for (const auto& placement : placements) {
        int x = start_x + static_cast<int>(placement.x);
        drawCharacter(world, placement.text, x, start_y);
    }
}

void ClockScenario::drawTime(World& world)
{
    std::string time_str = getCurrentTimeString();
    drawTimeString(world, time_str);
    last_drawn_time_ = time_str;
}

std::string ClockScenario::getCurrentTimeString() const
{
    // Return override if set (for testing).
    if (time_override_) {
        return *time_override_;
    }

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

    // Format: "H H : M M : S S" - spaces control all gaps.
    // Each space advances cursor by digit gap width.
    char buffer[48];
    if (config_.showSeconds) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%d %d : %d %d : %d %d",
            time_info->tm_hour / 10,
            time_info->tm_hour % 10,
            time_info->tm_min / 10,
            time_info->tm_min % 10,
            time_info->tm_sec / 10,
            time_info->tm_sec % 10);
    }
    else {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%d %d : %d %d",
            time_info->tm_hour / 10,
            time_info->tm_hour % 10,
            time_info->tm_min / 10,
            time_info->tm_min % 10);
    }
    return std::string(buffer);
}

void ClockScenario::setTimeOverride(const std::string& time_str)
{
    time_override_ = time_str;
}

void ClockScenario::clearTimeOverride()
{
    time_override_.reset();
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

    // Check for time change - set flag and trigger OnTimeChange events.
    std::string current_time = getCurrentTimeString();
    time_changed_this_frame_ = (current_time != last_trigger_check_time_);
    if (time_changed_this_frame_) {
        last_trigger_check_time_ = current_time;
        tryTriggerTimeChangeEvents(world);
    }

    // Periodic trigger check (once per second).
    time_since_last_trigger_check_ += deltaTime;
    if (time_since_last_trigger_check_ >= 1.0) {
        time_since_last_trigger_check_ = 0.0;
        tryTriggerPeriodicEvents(world);
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

void ClockScenario::tryTriggerPeriodicEvents(World& world)
{
    if (!first_tick_done_) {
        return;
    }

    static constexpr std::array<ClockEventType, 7> ALL_EVENT_TYPES = {
        ClockEventType::COLOR_CYCLE, ClockEventType::COLOR_SHOWCASE, ClockEventType::DIGIT_SLIDE,
        ClockEventType::DUCK,        ClockEventType::MARQUEE,        ClockEventType::MELTDOWN,
        ClockEventType::RAIN,
    };

    for (ClockEventType type : ALL_EVENT_TYPES) {
        const auto& timing = getEventTiming(type);
        if (timing.trigger_type != EventTriggerType::Periodic) {
            continue;
        }

        if (active_events_.contains(type)) {
            continue;
        }

        if (event_cooldowns_.contains(type) && event_cooldowns_[type] > 0.0) {
            continue;
        }

        double effective_chance = timing.chance * config_.eventFrequency;
        if (uniform_dist_(rng_) < effective_chance) {
            startEvent(world, type);
        }
    }
}

void ClockScenario::tryTriggerTimeChangeEvents(World& world)
{
    if (!first_tick_done_) {
        return;
    }

    static constexpr std::array<ClockEventType, 7> ALL_EVENT_TYPES = {
        ClockEventType::COLOR_CYCLE, ClockEventType::COLOR_SHOWCASE, ClockEventType::DIGIT_SLIDE,
        ClockEventType::DUCK,        ClockEventType::MARQUEE,        ClockEventType::MELTDOWN,
        ClockEventType::RAIN,
    };

    for (ClockEventType type : ALL_EVENT_TYPES) {
        const auto& timing = getEventTiming(type);
        if (timing.trigger_type != EventTriggerType::OnTimeChange) {
            continue;
        }

        if (active_events_.contains(type)) {
            continue;
        }

        if (event_cooldowns_.contains(type) && event_cooldowns_[type] > 0.0) {
            continue;
        }

        double effective_chance = timing.chance * config_.eventFrequency;
        if (effective_chance >= 1.0 || uniform_dist_(rng_) < effective_chance) {
            startEvent(world, type);
        }
    }
}

void ClockScenario::startEvent(World& world, ClockEventType type)
{
    const auto& eventTiming = getEventTiming(type);

    ActiveEvent event{};
    event.remaining_time = eventTiming.duration;

    if (type == ClockEventType::COLOR_CYCLE) {
        ColorCycleEventState state;
        Material::EnumType starting_material =
            ClockEvents::startColorCycle(state, config_.colorsPerSecond);
        config_.digitMaterial = starting_material;
        event.state = state;
        config_.colorCycleEnabled = true; // Sync config flag.
        spdlog::info(
            "ClockScenario: Starting COLOR_CYCLE event (duration: {}s, rate: {} colors/sec)",
            eventTiming.duration,
            config_.colorsPerSecond);
    }
    else if (type == ClockEventType::MELTDOWN) {
        MeltdownEventState melt_state;
        ClockEvents::startMeltdown(melt_state, world);
        event.state = melt_state;
        spdlog::info(
            "ClockScenario: Starting MELTDOWN event (duration: {}s)", eventTiming.duration);
    }
    else if (type == ClockEventType::RAIN) {
        event.state = RainEventState{};
        config_.rainEnabled = true; // Sync config flag.
        spdlog::info("ClockScenario: Starting RAIN event (duration: {}s)", eventTiming.duration);
    }
    else if (type == ClockEventType::COLOR_SHOWCASE) {
        ColorShowcaseEventState state;
        const auto& showcase_materials = event_configs_.color_showcase.showcase_materials;
        Material::EnumType starting_material =
            ClockEvents::startColorShowcase(state, showcase_materials, rng_);
        config_.digitMaterial = starting_material;
        event.state = state;
        config_.colorShowcaseEnabled = true; // Sync config flag.
        spdlog::info(
            "ClockScenario: Starting COLOR_SHOWCASE event (duration: {}s, starting color: {} at "
            "index {})",
            eventTiming.duration,
            toString(starting_material),
            state.current_index);
    }
    else if (type == ClockEventType::DIGIT_SLIDE) {
        DigitSlideEventState slide_event_state;
        initVerticalSlide(
            slide_event_state.slide_state,
            event_configs_.digit_slide.animation_speed,
            getDigitHeight());
        // Initialize with current time so the next change triggers animation.
        slide_event_state.slide_state.new_time_str = getCurrentTimeString();
        event.state = slide_event_state;
        config_.digitSlideEnabled = true; // Sync config flag.
        spdlog::info(
            "ClockScenario: Starting DIGIT_SLIDE event (speed: {})",
            event_configs_.digit_slide.animation_speed);
    }
    else if (type == ClockEventType::MARQUEE) {
        MarqueeEventState marquee_state;
        std::string time_str = getCurrentTimeString();
        double visible_width = static_cast<double>(world.getData().width);
        const CharacterMetrics& metrics = getMetrics();
        startHorizontalScroll(
            marquee_state.scroll_state,
            time_str,
            visible_width,
            event_configs_.marquee.scroll_speed,
            metrics.widthFunction());
        event.state = marquee_state;
        config_.marqueeEnabled = true; // Sync config flag.
        spdlog::info(
            "ClockScenario: Starting MARQUEE event (duration: {}s, speed: {})",
            eventTiming.duration,
            event_configs_.marquee.scroll_speed);
    }
    else if (type == ClockEventType::DUCK) {
        DuckEventState duck_state;

        // Choose random entrance side. Door is 1 cell above floor.
        duck_state.entrance_side = (uniform_dist_(rng_) < 0.5) ? DoorSide::LEFT : DoorSide::RIGHT;
        constexpr uint32_t kCellsAboveFloor = 1;

        // Create entrance door (DoorManager computes position from side + cells_above_floor).
        duck_state.entrance_door_id =
            door_manager_.createDoor(duck_state.entrance_side, kCellsAboveFloor);

        // Create exit door on opposite side at same height.
        DoorSide exit_side =
            (duck_state.entrance_side == DoorSide::LEFT) ? DoorSide::RIGHT : DoorSide::LEFT;
        duck_state.exit_door_id = door_manager_.createDoor(exit_side, kCellsAboveFloor);

        // Add indicator lights for both doors.
        // Door lights have short radius and quick falloff - subtle indicator effect.
        constexpr float kDoorLightRadius = 6.0f;
        constexpr float kDoorLightAttenuation = 0.25f;
        constexpr float kDoorLightOpenIntensity = 0.4f;
        constexpr float kDoorLightClosedIntensity = 0.08f;

        const WorldData& data = world.getData();
        LightManager& lights = world.getLightManager();

        // Entrance door light (will be bright since door opens immediately).
        Vector2i entrance_light_pos =
            door_manager_.getLightPosition(duck_state.entrance_door_id, data);
        duck_state.entrance_light = lights.createLight(
            PointLight{ .position = Vector2d{ static_cast<double>(entrance_light_pos.x),
                                              static_cast<double>(entrance_light_pos.y) },
                        .color = ColorNames::torchOrange(),
                        .intensity = kDoorLightOpenIntensity,
                        .radius = kDoorLightRadius,
                        .attenuation = kDoorLightAttenuation });

        // Exit door light (starts dim since door is closed).
        Vector2i exit_light_pos = door_manager_.getLightPosition(duck_state.exit_door_id, data);
        duck_state.exit_light = lights.createLight(
            PointLight{ .position = Vector2d{ static_cast<double>(exit_light_pos.x),
                                              static_cast<double>(exit_light_pos.y) },
                        .color = ColorNames::torchOrange(),
                        .intensity = kDoorLightClosedIntensity,
                        .radius = kDoorLightRadius,
                        .attenuation = kDoorLightAttenuation });

        // Open entrance door via DoorManager. Duck spawns after delay.
        door_manager_.openDoor(duck_state.entrance_door_id, world);
        duck_state.phase = DuckEventPhase::DOOR_OPENING;
        duck_state.door_open_timer = 0.0;

        spdlog::info(
            "ClockScenario: Opening {} door for duck entrance",
            duck_state.entrance_side == DoorSide::LEFT ? "LEFT" : "RIGHT");

        event.state = std::move(duck_state);
        config_.duckEnabled = true; // Sync config flag.
        spdlog::info("ClockScenario: Starting DUCK event (duration: {}s)", eventTiming.duration);
    }

    active_events_[type] = std::move(event);
}

void ClockScenario::updateEvent(
    World& world, ClockEventType /*type*/, ActiveEvent& event, double deltaTime)
{
    std::visit(
        [&](auto& state) {
            using T = std::decay_t<decltype(state)>;
            if constexpr (std::is_same_v<T, ColorCycleEventState>) {
                updateColorCycleEvent(world, state, deltaTime);
            }
            else if constexpr (std::is_same_v<T, ColorShowcaseEventState>) {
                updateColorShowcaseEvent(world, state, deltaTime);
            }
            else if constexpr (std::is_same_v<T, DigitSlideEventState>) {
                updateDigitSlideEvent(world, state, deltaTime);
            }
            else if constexpr (std::is_same_v<T, DuckEventState>) {
                updateDuckEvent(world, state, event.remaining_time, deltaTime);
            }
            else if constexpr (std::is_same_v<T, MarqueeEventState>) {
                updateMarqueeEvent(world, state, event.remaining_time, deltaTime);
            }
            else if constexpr (std::is_same_v<T, MeltdownEventState>) {
                updateMeltdownEvent(world, state, event.remaining_time, deltaTime);
            }
            else if constexpr (std::is_same_v<T, RainEventState>) {
                updateRainEvent(world, state, deltaTime);
            }
        },
        event.state);
}

void ClockScenario::updateColorCycleEvent(
    World& /*world*/, ColorCycleEventState& state, double deltaTime)
{
    auto new_material = ClockEvents::updateColorCycle(state, deltaTime);
    if (new_material) {
        config_.digitMaterial = *new_material;
    }
}

void ClockScenario::updateColorShowcaseEvent(
    World& /*world*/, ColorShowcaseEventState& state, double /*deltaTime*/)
{
    const auto& showcase_materials = event_configs_.color_showcase.showcase_materials;
    auto new_material =
        ClockEvents::updateColorShowcase(state, showcase_materials, time_changed_this_frame_);
    if (new_material) {
        config_.digitMaterial = *new_material;
    }
}

void ClockScenario::updateDigitSlideEvent(
    World& world, DigitSlideEventState& state, double deltaTime)
{
    std::string current_time = getCurrentTimeString();

    // Check if time changed and start a new slide animation if needed.
    checkAndStartSlide(state.slide_state, last_drawn_time_, current_time);

    // Update animation and render if active.
    if (state.slide_state.active) {
        const CharacterMetrics& metrics = getMetrics();
        auto getWidth = metrics.widthFunction();
        MarqueeFrame frame = updateVerticalSlide(state.slide_state, deltaTime, getWidth);

        // Clear previous digits.
        clearDigits(world);

        // Get font dimensions.
        int dh = metrics.getHeight();

        // Calculate centering (same as drawTimeString).
        int content_width = calculateStringWidth(current_time, getWidth);
        int start_x = (world.getData().width - content_width) / 2;
        int start_y = (world.getData().height - dh) / 2;

        // Draw each character from the frame.
        for (const auto& placement : frame.placements) {
            // Apply centering.
            int x = start_x + static_cast<int>(placement.x);
            int y = start_y + static_cast<int>(placement.y);

            // Skip if off-screen (clipping).
            if (y + dh < 0 || y >= world.getData().height) {
                continue;
            }
            int charWidth = getWidth(placement.text);
            if (x + charWidth < 0 || x >= world.getData().width) {
                continue;
            }

            drawCharacter(world, placement.text, x, y);
        }
    }

    // Keep track of last drawn time for next comparison.
    // Note: last_drawn_time_ is updated by drawTime() normally, but we need to track it here too.
    if (!state.slide_state.active) {
        // Animation finished, update time string for next change detection.
        state.slide_state.new_time_str = current_time;
    }
}

void ClockScenario::updateRainEvent(World& world, RainEventState& /*state*/, double deltaTime)
{
    ClockEvents::updateRain(world, deltaTime, rng_, uniform_dist_);
}

void ClockScenario::updateMarqueeEvent(
    World& world, MarqueeEventState& state, double& remaining_time, double deltaTime)
{
    std::string time_str = getCurrentTimeString();
    const CharacterMetrics& metrics = getMetrics();
    auto getWidth = metrics.widthFunction();
    MarqueeFrame frame = updateHorizontalScroll(state.scroll_state, time_str, deltaTime, getWidth);

    // Clear previous digits.
    clearDigits(world);

    // Get font dimensions.
    int dh = metrics.getHeight();

    // Calculate centering (same as drawTimeString).
    int content_width = static_cast<int>(state.scroll_state.content_width);
    int start_x = (world.getData().width - content_width) / 2;
    int start_y = (world.getData().height - dh) / 2;

    // Draw each character from the frame, offset by viewport and centering.
    for (const auto& placement : frame.placements) {
        // Apply centering and viewport offset.
        double screen_x = start_x + placement.x - frame.viewportX;
        int charWidth = getWidth(placement.text);

        // Skip if off-screen.
        if (screen_x + charWidth < 0 || screen_x >= static_cast<double>(world.getData().width)) {
            continue;
        }

        int x = static_cast<int>(screen_x);
        drawCharacter(world, placement.text, x, start_y);
    }

    // Signal event completion after drawing the final frame.
    if (frame.finished) {
        remaining_time = 0.0;
    }
}

void ClockScenario::spawnDuck(World& world, DuckEventState& state)
{
    // Spawn duck in the door opening.
    Vector2i spawn_pos = door_manager_.getDoorPosition(state.entrance_door_id, world.getData());

    // Check if spawn location is blocked by another organism.
    OrganismId blocking = world.getOrganismManager().at(spawn_pos);
    if (blocking != INVALID_ORGANISM_ID) {
        // Try to displace the blocking organism to an adjacent empty cell.
        static constexpr std::array<std::pair<int, int>, 4> directions = {
            { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
        };

        Vector2i best_neighbor{ -1, -1 };
        for (auto [dx, dy] : directions) {
            int nx = spawn_pos.x + dx;
            int ny = spawn_pos.y + dy;

            if (!world.getData().inBounds(nx, ny)) {
                continue;
            }

            Vector2i neighbor_pos{ nx, ny };

            // Skip if occupied by another organism.
            if (world.getOrganismManager().at(neighbor_pos) != INVALID_ORGANISM_ID) {
                continue;
            }

            // Skip walls.
            const Cell& neighbor = world.getData().at(nx, ny);
            if (neighbor.material_type == Material::EnumType::Wall) {
                continue;
            }

            best_neighbor = neighbor_pos;
            break;
        }

        if (best_neighbor.x < 0) {
            // Can't displace - skip spawn for this frame.
            spdlog::info(
                "ClockScenario: Cannot displace organism {} from spawn location ({}, {}), "
                "waiting...",
                blocking,
                spawn_pos.x,
                spawn_pos.y);
            return;
        }

        // Displace the blocking organism by swapping cells.
        spdlog::info(
            "ClockScenario: Displacing organism {} from ({},{}) to ({},{}) for duck spawn",
            blocking,
            spawn_pos.x,
            spawn_pos.y,
            best_neighbor.x,
            best_neighbor.y);
        world.swapCells(spawn_pos, best_neighbor);
    }

    // Use DuckBrain2 with dead reckoning and exit-seeking behavior.
    std::unique_ptr<DuckBrain> brain = std::make_unique<DuckBrain2>();

    state.organism_id = world.getOrganismManager().createDuck(
        world,
        static_cast<uint32_t>(spawn_pos.x),
        static_cast<uint32_t>(spawn_pos.y),
        std::move(brain));

    spdlog::info(
        "ClockScenario: Duck organism {} enters through {} door at ({}, {})",
        state.organism_id,
        state.entrance_side == DoorSide::LEFT ? "LEFT" : "RIGHT",
        spawn_pos.x,
        spawn_pos.y);

    Duck* duck = world.getOrganismManager().getDuck(state.organism_id);
    if (duck) {
        LightHandle flashlight = world.getLightManager().createLight(
            SpotLight{ .position = Vector2d{ static_cast<double>(spawn_pos.x),
                                             static_cast<double>(spawn_pos.y) },
                       .color = ColorNames::warmSunlight(),
                       .intensity = 1.0f,
                       .radius = 15.0f,
                       .attenuation = 0.1f,
                       .direction = 0.0f,
                       .arc_width = static_cast<float>(M_PI / 3.0),
                       .focus = 1.0f });
        duck->attachLight(std::move(flashlight), true);
        spdlog::info("ClockScenario: Attached flashlight to duck {}", state.organism_id);
    }

    state.phase = DuckEventPhase::DUCK_ACTIVE;
}

void ClockScenario::updateDuckEvent(
    World& world, DuckEventState& state, double& remaining_time, double deltaTime)
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
        constexpr double SPAWN_INTERVAL = 3.0;

        state.obstacle_spawn_timer += deltaTime;
        if (state.obstacle_spawn_timer >= SPAWN_INTERVAL) {
            state.obstacle_spawn_timer = 0.0;
            obstacle_manager_.spawnObstacle(world, rng_, uniform_dist_);
        }
    }
    else if (drain_open_ && !obstacle_manager_.getObstacles().empty()) {
        // Drain is open, clear any obstacles.
        obstacle_manager_.clearAll(world);
    }

    // Get duck's cell COM for sub-cell positioning.
    const WorldData& data = world.getData();
    LightManager& lights = world.getLightManager();
    Vector2d duck_com{ 0.0, 0.0 };
    if (data.inBounds(duck_cell.x, duck_cell.y)) {
        duck_com = data.at(duck_cell.x, duck_cell.y).com;
    }

    // Door light intensity constants.
    constexpr float kDoorLightOpenIntensity = 1.0f;
    constexpr float kDoorLightClosedIntensity = 0.08f;

    // Close entrance door once duck moves away from it and schedule removal.
    Vector2i entrance_pos = door_manager_.getDoorPosition(state.entrance_door_id, data);
    if (door_manager_.isOpen(state.entrance_door_id) && duck_cell != entrance_pos) {
        door_manager_.closeDoor(state.entrance_door_id, world);
        door_manager_.scheduleRemoval(state.entrance_door_id, std::chrono::seconds(2));

        // Dim the entrance door light.
        if (state.entrance_light) {
            if (auto* light = lights.getLight<PointLight>(state.entrance_light->id())) {
                light->intensity = kDoorLightClosedIntensity;
            }
        }
    }

    // Open exit door in the last 7 seconds.
    if (!door_manager_.isOpen(state.exit_door_id) && remaining_time <= 7.0) {
        door_manager_.openDoor(state.exit_door_id, world);

        // Brighten the exit door light.
        if (state.exit_light) {
            if (auto* light = lights.getLight<PointLight>(state.exit_light->id())) {
                light->intensity = kDoorLightOpenIntensity;
            }
        }

        // Log world state when exit door opens.
        Vector2i exit_pos = door_manager_.getDoorPosition(state.exit_door_id, data);
        spdlog::info("ClockScenario: Exit door opened at ({}, {})", exit_pos.x, exit_pos.y);
        std::string diagram = WorldDiagramGeneratorEmoji::generateEmojiDiagram(world);
        spdlog::info("\n{}", diagram);
    }

    // Check if duck entered the exit door and passed the middle of the cell.
    Vector2i exit_pos = door_manager_.getDoorPosition(state.exit_door_id, data);
    if (door_manager_.isOpen(state.exit_door_id) && duck_cell == exit_pos) {
        bool past_middle =
            (state.entrance_side == DoorSide::LEFT) ? (duck_com.x > 0.0) : (duck_com.x < 0.0);
        if (past_middle) {
            spdlog::info(
                "ClockScenario: Duck exited through door at ({}, {}), COM.x={:.2f}",
                exit_pos.x,
                exit_pos.y,
                duck_com.x);

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
        case ClockEventType::COLOR_CYCLE:
            return "COLOR_CYCLE";
        case ClockEventType::COLOR_SHOWCASE:
            return "COLOR_SHOWCASE";
        case ClockEventType::DIGIT_SLIDE:
            return "DIGIT_SLIDE";
        case ClockEventType::DUCK:
            return "DUCK";
        case ClockEventType::MARQUEE:
            return "MARQUEE";
        case ClockEventType::MELTDOWN:
            return "MELTDOWN";
        case ClockEventType::RAIN:
            return "RAIN";
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
    else if (type == ClockEventType::DIGIT_SLIDE) {
        config_.digitSlideEnabled = false;
    }
    else if (type == ClockEventType::DUCK) {
        config_.duckEnabled = false;
    }
    else if (type == ClockEventType::MARQUEE) {
        config_.marqueeEnabled = false;
    }
    else if (type == ClockEventType::RAIN) {
        config_.rainEnabled = false;
    }

    if (type == ClockEventType::COLOR_CYCLE) {
        // Restore default digit material.
        config_.digitMaterial = Material::EnumType::Metal;
        spdlog::info("ClockScenario: COLOR_CYCLE ended, restored digit material to METAL");
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
        obstacle_manager_.clearAll(world);

        // Remove door lights (RAII handles auto-cleanup).
        state.entrance_light.reset();
        state.exit_light.reset();

        // Close doors and schedule removal after a delay.
        door_manager_.closeDoor(state.entrance_door_id, world);
        door_manager_.closeDoor(state.exit_door_id, world);
        door_manager_.scheduleRemoval(state.entrance_door_id, std::chrono::seconds(2));
        door_manager_.scheduleRemoval(state.exit_door_id, std::chrono::seconds(2));
    }
    else if (type == ClockEventType::COLOR_SHOWCASE) {
        // Restore default digit material (METAL).
        config_.digitMaterial = Material::EnumType::Metal;
        spdlog::info("ClockScenario: COLOR_SHOWCASE ended, restored digit material to METAL");
    }

    // Set cooldown for this event type.
    const auto& timing = getEventTiming(type);
    event_cooldowns_[type] = timing.cooldown;

    spdlog::info(
        "ClockScenario: Event {} on cooldown for {:.1f}s", eventTypeName(type), timing.cooldown);
}

void ClockScenario::cancelAllEvents(World& world)
{
    spdlog::info("ClockScenario: Canceling all events");

    for (auto& [type, event] : active_events_) {
        if (type == ClockEventType::COLOR_CYCLE) {
            // Restore default digit material.
            config_.digitMaterial = Material::EnumType::Metal;
        }
        else if (type == ClockEventType::DUCK) {
            auto& state = std::get<DuckEventState>(event.state);
            if (state.organism_id != INVALID_ORGANISM_ID) {
                world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
            }

            // Remove door lights (RAII handles auto-cleanup).
            state.entrance_light.reset();
            state.exit_light.reset();
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
    config_.digitSlideEnabled = false;
    config_.duckEnabled = false;
    config_.marqueeEnabled = false;
    config_.rainEnabled = false;

    active_events_.clear();
    event_cooldowns_.clear();
    door_manager_.closeAllDoors(world);
    obstacle_manager_.clearAll(world);
}

bool ClockScenario::isMeltdownActive() const
{
    return active_events_.contains(ClockEventType::MELTDOWN);
}

void ClockScenario::updateMeltdownEvent(
    World& world, MeltdownEventState& state, double& remaining_time, double /*deltaTime*/)
{
    double event_duration = getEventTiming(ClockEventType::MELTDOWN).duration;
    ClockEvents::updateMeltdown(
        state, world, remaining_time, event_duration, drain_open_, drain_start_x_, drain_end_x_);

    // Make falling digit cells emissive so they glow while melting.
    const WorldData& data = world.getData();
    uint32_t color = getMaterialColor(config_.digitMaterial);
    float intensity = static_cast<float>(config_.digitEmissiveness);

    for (int y = 1; y < data.height - 1; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            if (data.at(x, y).material_type == state.digit_material) {
                world.getLightCalculator().setEmissive(x, y, color, intensity);
            }
        }
    }
}

void ClockScenario::convertStrayDigitMaterialToWater(
    World& world, Material::EnumType digit_material)
{
    ClockEvents::endMeltdown(world, digit_material);
    drawTime(world);
}

double ClockScenario::countWaterInBottomThird(const World& world) const
{
    const WorldData& data = world.getData();

    // Count water in the bottom 1/3 of the world.
    int bottom_third_start = (data.height * 2) / 3;
    double total_water = 0.0;

    for (int y = bottom_third_start; y < data.height - 1; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.material_type == Material::EnumType::Water) {
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
    constexpr double CLOSE_THRESHOLD = 1.0;       // Below this, drain is closed.
    constexpr double FULL_OPEN_THRESHOLD = 100.0; // At or above this, drain is fully open.
    constexpr uint32_t MAX_DRAIN_SIZE = 7;        // Maximum drain opening width.

    // Calculate target drain size based on water level (odd numbers only: 3, 5, 7).
    // Size 1 is only used as an animation transition step, not a sustained state.
    int16_t target_drain_size = 0;
    if (water_amount >= FULL_OPEN_THRESHOLD) {
        target_drain_size = MAX_DRAIN_SIZE;
    }
    else if (water_amount >= CLOSE_THRESHOLD) {
        // Linear interpolation from 3 to MAX_DRAIN_SIZE, quantized to odd numbers.
        double t = (water_amount - CLOSE_THRESHOLD) / (FULL_OPEN_THRESHOLD - CLOSE_THRESHOLD);
        uint32_t continuous_size = 3 + static_cast<uint32_t>(t * (MAX_DRAIN_SIZE - 3));

        // Round to nearest odd number (3, 5, 7).
        if (continuous_size % 2 == 0) {
            // Even number - round down to next lower odd number.
            target_drain_size = continuous_size - 1;
        }
        else {
            target_drain_size = continuous_size;
        }
    }
    // else target_drain_size remains 0 (closed).

    // If there's any water on the bottom playable row, ensure drain opens at least one cell.
    // This prevents water from pooling at the bottom with no way to drain.
    if (target_drain_size == 0) {
        int bottom_row = data.height - 2;
        for (int x = 1; x < data.width - 1; ++x) {
            if (data.at(x, bottom_row).material_type == Material::EnumType::Water) {
                target_drain_size = 1;
                break;
            }
        }
    }

    // Hysteresis: only change drain size one step per second.
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_drain_size_change_)
            .count();

    int16_t actual_drain_size = current_drain_size_;
    if (target_drain_size != current_drain_size_ && elapsed >= 1000) {
        // Step one size at a time (0 <-> 1 <-> 3 <-> 5 <-> 7).
        if (target_drain_size > current_drain_size_) {
            // Opening: step up.
            if (current_drain_size_ == 0) {
                actual_drain_size = 1;
            }
            else {
                actual_drain_size = static_cast<int16_t>(current_drain_size_ + 2);
            }
        }
        else {
            // Closing: step down.
            if (current_drain_size_ == 1) {
                actual_drain_size = 0;
            }
            else {
                actual_drain_size = static_cast<int16_t>(current_drain_size_ - 2);
            }
        }
        current_drain_size_ = actual_drain_size;
        last_drain_size_change_ = now;
    }

    int center_x = data.width / 2;
    int drain_y = data.height - 1; // Bottom wall row.

    // Calculate new drain bounds based on actual size (centered).
    int half_drain = static_cast<int>(actual_drain_size / 2);
    int new_start_x =
        (actual_drain_size > 0 && center_x > half_drain) ? center_x - half_drain : center_x;
    int new_end_x =
        (actual_drain_size > 0) ? std::min(new_start_x + actual_drain_size - 1, data.width - 2) : 0;

    // Ensure start doesn't go below 1 (keep wall border).
    if (new_start_x < 1) new_start_x = 1;

    // Track if drain size changed.
    bool drain_was_open = drain_open_;
    int old_start_x = static_cast<int>(drain_start_x_);
    int old_end_x = static_cast<int>(drain_end_x_);

    drain_open_ = (actual_drain_size > 0);
    drain_start_x_ = static_cast<int16_t>(new_start_x);
    drain_end_x_ = static_cast<int16_t>(new_end_x);

    // Update drain cells if size changed.
    if (drain_was_open || drain_open_) {
        // Restore wall on cells that are no longer in the drain.
        if (drain_was_open) {
            for (int x = old_start_x; x <= old_end_x; ++x) {
                bool still_open = drain_open_ && x >= new_start_x && x <= new_end_x;
                if (!still_open) {
                    world.replaceMaterialAtCell(
                        { static_cast<int16_t>(x), static_cast<int16_t>(drain_y) },
                        Material::EnumType::Wall);
                }
            }
        }

        // Ensure drain cells are clear (always, not just when newly opened).
        // This handles cases where obstacle clearing may have restored floor over drain.
        if (drain_open_) {
            for (int x = new_start_x; x <= new_end_x; ++x) {
                Cell& cell = data.at(x, drain_y);
                if (cell.material_type == Material::EnumType::Wall) {
                    cell = Cell();
                }
            }
        }

        // Log significant changes.
        if (!drain_was_open && drain_open_) {
            spdlog::info(
                "ClockScenario: Drain opened (size: {}, water: {:.1f})",
                actual_drain_size,
                water_amount);
        }
        else if (drain_was_open && !drain_open_) {
            spdlog::info("ClockScenario: Drain closed (water: {:.1f})", water_amount);
        }
    }

    // If drain is open, handle material in drain cells.
    if (drain_open_) {
        int16_t center_x = static_cast<int16_t>((drain_start_x_ + drain_end_x_) / 2);

        // Get the digit material if a meltdown is active.
        Material::EnumType melt_digit_material =
            Material::EnumType::Air; // Default (won't match anything).
        if (isMeltdownActive()) {
            auto it = active_events_.find(ClockEventType::MELTDOWN);
            if (it != active_events_.end()) {
                melt_digit_material = std::get<MeltdownEventState>(it->second.state).digit_material;
            }
        }

        for (int16_t x = drain_start_x_; x <= drain_end_x_; ++x) {
            Cell& cell = data.at(x, drain_y);

            // Digit material falls through the drain during meltdown - convert to water and spray.
            if (cell.material_type == melt_digit_material && cell.com.y > 0.0) {
                cell.replaceMaterial(Material::EnumType::Water, cell.fill_ratio);
                sprayDrainCell(world, cell, x, drain_y);
                continue;
            }

            if (cell.material_type != Material::EnumType::Water) {
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

            // All drain cells dissipate: full to empty pretty fast.
            cell.fill_ratio -= (deltaTime * 10);
            if (cell.fill_ratio <= 0.0) {
                cell = Cell();
            }
        }

        // Drain center position.
        double drain_center_x = static_cast<double>(drain_start_x_ + drain_end_x_) / 2.0;
        double drain_center_y = static_cast<double>(drain_y);

        // Apply global gravity-like pull toward drain for all water in the world.
        constexpr double DRAIN_GRAVITY = 1.0; // Gentle pull toward drain.

        for (int y = 1; y < data.height - 1; ++y) {
            for (int x = 1; x < data.width - 1; ++x) {
                Cell& cell = data.at(x, y);
                if (cell.material_type != Material::EnumType::Water) {
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
        uint32_t bottom_row = drain_y - 1; // Row above the drain (height - 2).
        double max_distance = static_cast<double>(data.width) / 2.0;
        constexpr double MAX_FORCE = 5.0; // Maximum suction force.

        for (int x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, bottom_row);
            if (cell.material_type != Material::EnumType::Water) {
                continue;
            }

            // Calculate distance to drain center.
            double cell_x = static_cast<double>(x);
            double distance = std::abs(cell_x - drain_center_x);

            // Force strength: stronger when close.
            double strength = 1.0 - 0.9 * std::min(distance / max_distance, 1.0);
            double force_magnitude = MAX_FORCE * strength;

            // Pull water down when directly over the drain opening.
            bool over_drain = (x >= drain_start_x_ && x <= drain_end_x_);
            double downward_force = over_drain ? MAX_FORCE : 0.0;

            double horizontal_force;
            if (over_drain) {
                // Apply horizontal damping to prevent overshooting the drain.
                // Damping opposes horizontal velocity with magnitude equal to suction force.
                horizontal_force = -cell.velocity.x * force_magnitude;
            }
            else {
                // Direction toward drain center.
                double direction = (cell_x < drain_center_x) ? 1.0 : -1.0;
                if (std::abs(cell_x - drain_center_x) < 0.5) {
                    direction = 0.0; // Already at drain.
                }
                horizontal_force = direction * force_magnitude;
            }

            cell.addPendingForce(Vector2d{ horizontal_force, downward_force });
        }
    }
}

void ClockScenario::sprayDrainCell(World& world, Cell& cell, int16_t x, int16_t y)
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
        world, cell, x, y, x, y, spray_direction, NUM_FRAGS, ARC_WIDTH, drain_frag_params);

    cell = Cell();
}

std::vector<ClockScenario::WallSpec> ClockScenario::generateWallSpecs(const WorldData& data) const
{
    int16_t width = data.width;
    int16_t height = data.height;

    // Pre-allocate for efficiency: border cells + potential hurdles + roof cells.
    std::vector<WallSpec> walls;
    walls.reserve(2 * (width + height) + 20);

    // Top border (wooden frame - blocks sunlight, emissive digits glow in darkness).
    for (int16_t x = 0; x < width; ++x) {
        walls.push_back({ x, 0, Material::EnumType::Wood });
    }

    // Bottom border (dirt floor).
    for (int16_t x = 0; x < width; ++x) {
        Vector2i pos{ x, static_cast<int>(height - 1) };

        // Skip open doors, drain cells, and pit cells.
        bool is_drain_cell = drain_open_ && x >= drain_start_x_ && x <= drain_end_x_;
        bool is_pit_cell = obstacle_manager_.isPitAt(x);

        if (!door_manager_.isOpenDoorAt(pos, data) && !is_drain_cell && !is_pit_cell) {
            walls.push_back({ x, static_cast<int16_t>(height - 1), Material::EnumType::Dirt });
        }
    }

    // Left border (wooden frame).
    for (int16_t y = 0; y < height; ++y) {
        Vector2i pos{ 0, y };
        if (!door_manager_.isOpenDoorAt(pos, data)) {
            walls.push_back({ 0, y, Material::EnumType::Wood });
        }
    }

    // Right border (wooden frame).
    for (int16_t y = 0; y < height; ++y) {
        Vector2i pos{ width - 1, y };
        if (!door_manager_.isOpenDoorAt(pos, data)) {
            walls.push_back({ static_cast<int16_t>(width - 1), y, Material::EnumType::Wood });
        }
    }

    // Hurdle obstacles (one row above floor, render as wall/gray).
    if (height > 2) {
        for (int16_t x = 0; x < width; ++x) {
            if (obstacle_manager_.isHurdleAt(x)) {
                walls.push_back({ x, static_cast<int16_t>(height - 2), Material::EnumType::Wall });
            }
        }
    }

    // Door roof cells (structural, render as wall/gray).
    for (const auto& roof_pos : door_manager_.getRoofPositions(data)) {
        walls.push_back({ static_cast<int16_t>(roof_pos.x),
                          static_cast<int16_t>(roof_pos.y),
                          Material::EnumType::Wall });
    }

    // Door frame cells (wall above door, floor at door - render as wall/gray).
    for (const auto& frame_pos : door_manager_.getFramePositions(data)) {
        walls.push_back({ static_cast<int16_t>(frame_pos.x),
                          static_cast<int16_t>(frame_pos.y),
                          Material::EnumType::Wall });
    }

    return walls;
}

void ClockScenario::applyWalls(World& world, const std::vector<WallSpec>& walls)
{
    for (const auto& wall : walls) {
        world.replaceMaterialAtCell({ wall.x, wall.y }, Material::EnumType::Wall);
        world.getData().at(wall.x, wall.y).render_as = static_cast<int8_t>(wall.render_as);
    }
}

void ClockScenario::redrawWalls(World& world)
{
    const WorldData& data = world.getData();

    // Generate and apply wall specs.
    std::vector<WallSpec> walls = generateWallSpecs(data);
    applyWalls(world, walls);

    // Clear pit cells that shouldn't have walls.
    int16_t height = data.height;
    for (int16_t x = 0; x < data.width; ++x) {
        bool is_pit_cell = obstacle_manager_.isPitAt(x);
        bool is_drain_cell = drain_open_ && x >= drain_start_x_ && x <= drain_end_x_;

        if (is_pit_cell && !is_drain_cell) {
            Cell& cell = world.getData().at(x, height - 1);
            if (cell.material_type == Material::EnumType::Wall) {
                cell = Cell();
            }
        }
    }
}

} // namespace DirtSim
