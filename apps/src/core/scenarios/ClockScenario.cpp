#include "ClockScenario.h"
#include "clock_scenario/CharacterMetrics.h"
#include "clock_scenario/DoorEntrySpawn.h"
#include "clock_scenario/GlowManager.h"
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
#include "core/organisms/components/LightHandHeld.h"
#include "spdlog/spdlog.h"

#include <algorithm>
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

static const char* eventTypeName(ClockEventType type);

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
    return event_manager_.isEventActive(type);
}

size_t ClockScenario::getActiveEventCount() const
{
    return event_manager_.getActiveEventCount();
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

bool ClockScenario::triggerEvent(World& world, ClockEventType type)
{
    if (event_manager_.isEventActive(type)) {
        spdlog::info(
            "ClockScenario: Ignoring manual {} trigger (already active)", eventTypeName(type));
        return false;
    }

    if (isEventBlockedByConflict(type)) {
        queueEvent(type);
        return true;
    }

    startEvent(world, type);
    return true;
}

int ClockScenario::getDigitWidth() const
{
    return getFont(config_.font).digitWidth;
}
int ClockScenario::getDigitHeight() const
{
    return getFont(config_.font).digitHeight;
}
int ClockScenario::getDigitGap() const
{
    return getFont(config_.font).gap;
}
int ClockScenario::getColonWidth() const
{
    return getFont(config_.font).colonWidth;
}
int ClockScenario::getColonPadding() const
{
    return getFont(config_.font).colonPadding;
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
    return getFont(config_.font);
}

void ClockScenario::recalculateDimensions()
{
    int clock_width = calculateTotalWidth();
    int clock_height = getDigitHeight();

    constexpr int BUFFER = 4;

    if (!config_.autoScale || config_.targetDisplayWidth == 0 || config_.targetDisplayHeight == 0) {
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
        return;
    }

    double display_aspect =
        static_cast<double>(config_.targetDisplayWidth) / config_.targetDisplayHeight;

    int world_width, world_height;

    if (config_.targetDigitHeightPercent > 0) {
        // Target height mode: prioritize achieving target height over aspect ratio matching.
        // pixel_height = cell_height * (display_height / world_height) = target
        // Solving: world_height = cell_height * display_height / target_pixels
        //
        // Gray bars may appear on sides if clock aspect doesn't match display aspect.
        // This trade-off ensures all fonts render at the same pixel height.
        double target_pixels =
            config_.targetDisplayHeight * config_.targetDigitHeightPercent / 100.0;

        // Calculate world height to achieve exact target.
        world_height =
            static_cast<int>(std::ceil(clock_height * config_.targetDisplayHeight / target_pixels));

        // Ensure height accommodates clock.
        if (world_height < clock_height) {
            world_height = clock_height;
        }

        // Width: just ensure clock fits (don't force display aspect).
        world_width = clock_width;

        spdlog::info(
            "ClockScenario: Target height {}% - display={}x{}, clock={}x{}, world={}x{} (height "
            "prioritized, aspect={})",
            config_.targetDigitHeightPercent,
            config_.targetDisplayWidth,
            config_.targetDisplayHeight,
            clock_width,
            clock_height,
            world_width,
            world_height,
            static_cast<double>(world_width) / world_height);
    }
    else {
        // Aspect-matching mode: size world to fit clock tightly, matching display aspect.
        int base_width = clock_width + 2 * BUFFER;
        int base_height = clock_height + 2 * BUFFER;
        double clock_aspect = static_cast<double>(base_width) / base_height;

        if (display_aspect > clock_aspect) {
            world_height = base_height;
            world_width =
                std::max(base_width, static_cast<int>(std::round(world_height * display_aspect)));
        }
        else {
            world_width = base_width;
            world_height =
                std::max(base_height, static_cast<int>(std::round(world_width / display_aspect)));
        }

        spdlog::info(
            "ClockScenario: Auto-scale - display={}x{}, clock={}x{}, world={}x{} (aspect matched)",
            config_.targetDisplayWidth,
            config_.targetDisplayHeight,
            clock_width,
            clock_height,
            world_width,
            world_height);
    }

    config_.horizontalScale = 1.0;
    config_.verticalScale = 1.0;
    metadata_.requiredWidth = static_cast<uint32_t>(world_width);
    metadata_.requiredHeight = static_cast<uint32_t>(world_height);
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
            || (incoming.targetDigitHeightPercent != config_.targetDigitHeightPercent)
            || (incoming.marginPixels != config_.marginPixels);
        const bool obstacle_course_changed =
            incoming.obstacleCourseEnabled != config_.obstacleCourseEnabled;

        auto resizeWorldToMetadata = [&](World& world_to_resize) {
            if (metadata_.requiredWidth == 0 || metadata_.requiredHeight == 0) {
                return;
            }

            const WorldData& data = world_to_resize.getData();
            if (data.width == static_cast<int>(metadata_.requiredWidth)
                && data.height == static_cast<int>(metadata_.requiredHeight)) {
                return;
            }

            spdlog::info(
                "ClockScenario: Resizing world to {}x{}",
                metadata_.requiredWidth,
                metadata_.requiredHeight);
            world_to_resize.resizeGrid(
                static_cast<int16_t>(metadata_.requiredWidth),
                static_cast<int16_t>(metadata_.requiredHeight));
        };

        config_ = incoming;

        if (layout_changed) {
            // Layout changes require recalculating dimensions and redrawing.
            recalculateDimensions();
            resizeWorldToMetadata(world);

            spdlog::info(
                "ClockScenario: Layout changed, resetting (font={}, showSeconds={})",
                static_cast<int>(config_.font),
                config_.showSeconds);

            cancelAllEvents(world);
            reset(world);
        }
        else if (dimensions_changed) {
            recalculateDimensions();
            resizeWorldToMetadata(world);

            spdlog::info(
                "ClockScenario: Dimensions changed (display={}x{})",
                config_.targetDisplayWidth,
                config_.targetDisplayHeight);

            clearDigits(world);
            drain_manager_.reset();
            storm_manager_.reset();

            redrawWalls(world);
            std::vector<Vector2i> tempDigitPositions;
            drawTime(world, tempDigitPositions);
        }

        // Stop any running events that are no longer allowed.
        auto stopEventIfDisabled = [&](ClockEventType type, bool enabled, const char* label) {
            if (enabled) {
                return;
            }
            ActiveEvent* event = event_manager_.getActiveEvent(type);
            if (!event) {
                return;
            }
            endEvent(world, type, *event, false);
            event_manager_.removeActiveEvent(type);
            spdlog::info("ClockScenario: {} disabled", label);
        };

        stopEventIfDisabled(ClockEventType::COLOR_CYCLE, config_.colorCycleEnabled, "Color cycle");
        stopEventIfDisabled(
            ClockEventType::COLOR_SHOWCASE, config_.colorShowcaseEnabled, "Color showcase");
        stopEventIfDisabled(ClockEventType::DIGIT_SLIDE, config_.digitSlideEnabled, "Digit slide");
        stopEventIfDisabled(ClockEventType::DUCK, config_.duckEnabled, "Duck");
        stopEventIfDisabled(ClockEventType::MARQUEE, config_.marqueeEnabled, "Marquee");
        stopEventIfDisabled(ClockEventType::MELTDOWN, config_.meltdownEnabled, "Meltdown");
        stopEventIfDisabled(ClockEventType::RAIN, config_.rainEnabled, "Rain");

        queued_events_.erase(
            std::remove_if(
                queued_events_.begin(),
                queued_events_.end(),
                [&](ClockEventType type) { return !isEventAllowed(type); }),
            queued_events_.end());

        updateDigitMaterialOverride();
        if (obstacle_course_changed && !config_.obstacleCourseEnabled) {
            obstacleSpawnTimer_ = 0.0;
            obstacle_manager_.clearAll(world);
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

    std::string current_time = getCurrentTimeString();
    event_manager_.updateTimeTracking(current_time, 0.0);

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

    std::vector<Vector2i> tempDigitPositions;
    drawTime(world, tempDigitPositions);

    // Add static torch lights at corners.
    world.getLightManager().clear();
    const WorldData& data = world.getData();

    // Static corner torches (fire-and-forget, LightManager owns them).
    world.getLightManager().addLight(
        PointLight{ .position =
                        Vector2d{ static_cast<double>(data.width - 2), static_cast<double>(2) },
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
    obstacleSpawnTimer_ = 0.0;
    setup(world);
}

void ClockScenario::tick(World& world, double deltaTime)
{
    first_tick_done_ = true;
    redrawWalls(world);

    std::vector<Vector2i> digitPositions;

    // Update event system first so digit slide can detect time changes
    // before drawTime() updates last_drawn_time_.
    updateEvents(world, deltaTime, digitPositions);

    // Process scheduled door removals.
    door_manager_.update();

    // Check if digit slide is animating (takes over rendering like marquee).
    bool digit_slide_animating = false;
    if (isEventActive(ClockEventType::DIGIT_SLIDE)) {
        ActiveEvent* event = event_manager_.getActiveEvent(ClockEventType::DIGIT_SLIDE);
        if (event) {
            const auto& state = std::get<DigitSlideEventState>(event->state);
            digit_slide_animating = state.slide_state.active;
        }
    }

    if (!isMeltdownActive() && !isEventActive(ClockEventType::MARQUEE) && !digit_slide_animating) {
        drawTime(world, digitPositions);
    }

    // Manage floor drain based on water level.
    double waterAmount = countWaterInBottomThird(world);
    std::optional<Material::EnumType> meltMaterial = std::nullopt;
    if (isMeltdownActive()) {
        ActiveEvent* event = event_manager_.getActiveEvent(ClockEventType::MELTDOWN);
        if (event) {
            meltMaterial = std::get<MeltdownEventState>(event->state).digit_material;
        }
    }
    drain_manager_.update(world, deltaTime, waterAmount, meltMaterial, rng_);
    updateFloorObstacles(world, deltaTime);

    // Manage storm lighting (lightning flashes based on water in top third).
    if (isEventActive(ClockEventType::RAIN)) {
        double topWater = countWaterInTopThird(world);
        double stormIntensity = std::min(topWater / 10.0, 1.0);
        storm_manager_.update(world.getLightCalculator(), deltaTime, stormIntensity, rng_);
    }

    // Apply glow to all emissive cells.
    {
        const WorldData& glowData = world.getData();
        std::vector<WallSpec> wallSpecs = generateWallSpecs(glowData);

        std::vector<Vector2i> floorPositions;
        std::vector<Vector2i> obstaclePositions;
        std::vector<Vector2i> wallPositions;

        for (const auto& spec : wallSpecs) {
            Vector2i pos{ spec.x, spec.y };
            switch (spec.render_as) {
                case Material::EnumType::Dirt:
                    floorPositions.push_back(pos);
                    break;
                case Material::EnumType::Wall:
                    obstaclePositions.push_back(pos);
                    break;
                case Material::EnumType::Wood:
                    wallPositions.push_back(pos);
                    break;
                case Material::EnumType::Air:
                case Material::EnumType::Leaf:
                case Material::EnumType::Metal:
                case Material::EnumType::Root:
                case Material::EnumType::Sand:
                case Material::EnumType::Seed:
                case Material::EnumType::Water:
                    break;
            }
        }

        for (const auto& door_pos : door_manager_.getOpenDoorPositions(glowData)) {
            obstaclePositions.push_back(door_pos);
        }

        GlowConfig glowConfig = config_.glowConfig;
        glowConfig.digitColor = getMaterialColor(getActiveDigitMaterial());

        GlowManager::apply(
            world, digitPositions, floorPositions, obstaclePositions, wallPositions, glowConfig);
    }

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
    World& world,
    const std::string& utf8Char,
    int start_x,
    int start_y,
    std::vector<Vector2i>& outDigitPositions)
{
    if (utf8Char.empty() || utf8Char == " ") {
        return;
    }

    if (config_.font == Config::ClockFont::NotoColorEmoji) {
        drawCharacterWithMaterials(world, utf8Char, start_x, start_y, outDigitPositions);
        return;
    }

    drawCharacterBinary(world, utf8Char, start_x, start_y, outDigitPositions);
}

void ClockScenario::drawCharacterBinary(
    World& world,
    const std::string& utf8Char,
    int start_x,
    int start_y,
    std::vector<Vector2i>& outDigitPositions)
{
    if (utf8Char.empty() || utf8Char == " ") {
        return;
    }

    int width = (utf8Char == ":") ? getColonWidth() : getDigitWidth();
    int height = getDigitHeight();

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int x = start_x + col;
            int y = start_y + row;

            if (x < 0 || x >= world.getData().width || y < 0 || y >= world.getData().height) {
                continue;
            }

            if (getCharacterPixel(utf8Char, row, col)) {
                placeDigitPixel(world, x, y, getActiveDigitMaterial(), outDigitPositions);
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
    World& world,
    const std::string& utf8Char,
    int start_x,
    int start_y,
    std::vector<Vector2i>& outDigitPositions)
{
    ensureFontSamplerInitialized();

    int dw = getDigitWidth();
    int dh = getDigitHeight();

    auto materialGrid = font_sampler_->sampleAndDownsample(utf8Char, dw, dh, 0.5f);

    if (materialGrid.width == 0 || materialGrid.height == 0) {
        spdlog::warn("ClockScenario: Failed to sample character '{}'", utf8Char);
        return;
    }

    for (int row = 0; row < materialGrid.height; ++row) {
        for (int col = 0; col < materialGrid.width; ++col) {
            int x = start_x + col;
            int y = start_y + row;

            if (x < 0 || x >= world.getData().width || y < 0 || y >= world.getData().height) {
                continue;
            }

            Material::EnumType mat = materialGrid.at(col, row);

            if (mat == Material::EnumType::Air) {
                continue;
            }

            placeDigitPixel(world, x, y, mat, outDigitPositions);
        }
    }
}

void ClockScenario::placeDigitPixel(
    World& world,
    int x,
    int y,
    Material::EnumType renderMaterial,
    std::vector<Vector2i>& outDigitPositions)
{
    world.replaceMaterialAtCell(
        { static_cast<int16_t>(x), static_cast<int16_t>(y) }, Material::EnumType::Wall);
    world.getData().at(x, y).render_as = static_cast<int8_t>(renderMaterial);
    outDigitPositions.push_back({ x, y });
}

void ClockScenario::drawTimeString(
    World& world, const std::string& time_str, std::vector<Vector2i>& outDigitPositions)
{
    clearDigits(world);

    const CharacterMetrics& metrics = getMetrics();
    auto getWidth = metrics.widthFunction();

    int total_width = calculateStringWidth(time_str, getWidth);
    int dh = metrics.digitHeight;
    int start_x = (world.getData().width - total_width) / 2;
    int start_y = (world.getData().height - dh) / 2;

    auto placements = layoutString(time_str, getWidth);
    for (const auto& placement : placements) {
        int x = start_x + static_cast<int>(placement.x);
        drawCharacter(world, placement.text, x, start_y, outDigitPositions);
    }
}

void ClockScenario::drawTime(World& world, std::vector<Vector2i>& outDigitPositions)
{
    std::string time_str = getCurrentTimeString();
    drawTimeString(world, time_str, outDigitPositions);
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

Material::EnumType ClockScenario::getActiveDigitMaterial() const
{
    if (digit_material_override_) {
        return *digit_material_override_;
    }

    return config_.digitMaterial;
}

Material::EnumType ClockScenario::getColorCycleMaterial(const ColorCycleEventState& state) const
{
    const auto& materials = Material::getAllTypes();
    if (materials.empty()) {
        return config_.digitMaterial;
    }

    size_t index = state.current_index % materials.size();
    return materials[index];
}

Material::EnumType ClockScenario::getColorShowcaseMaterial(
    const ColorShowcaseEventState& state) const
{
    const auto& materials = event_configs_.color_showcase.showcase_materials;
    if (materials.empty()) {
        return config_.digitMaterial;
    }

    size_t index = state.current_index % materials.size();
    return materials[index];
}

void ClockScenario::updateDigitMaterialOverride()
{
    digit_material_override_.reset();

    for (const auto& [type, event] : event_manager_.getActiveEvents()) {
        switch (type) {
            case ClockEventType::COLOR_CYCLE: {
                const auto& state = std::get<ColorCycleEventState>(event.state);
                digit_material_override_ = getColorCycleMaterial(state);
                break;
            }
            case ClockEventType::COLOR_SHOWCASE: {
                const auto& state = std::get<ColorShowcaseEventState>(event.state);
                digit_material_override_ = getColorShowcaseMaterial(state);
                break;
            }
            case ClockEventType::DIGIT_SLIDE:
            case ClockEventType::DUCK:
            case ClockEventType::MARQUEE:
            case ClockEventType::MELTDOWN:
            case ClockEventType::RAIN:
                break;
        }
    }
}

// ============================================================================
// Event System
// ============================================================================

void ClockScenario::updateEvents(
    World& world, double deltaTime, std::vector<Vector2i>& digitPositions)
{
    if (config_.eventFrequency <= 0.0) {
        return;
    }

    event_manager_.updateCooldowns(deltaTime);

    std::string current_time = getCurrentTimeString();
    event_manager_.updateTimeTracking(current_time, deltaTime);

    if (event_manager_.hasTimeChangedThisFrame()) {
        tryTriggerTimeChangeEvents(world);
    }

    if (event_manager_.shouldCheckPeriodicTriggers()) {
        event_manager_.resetTriggerCheckTimer();
        tryTriggerPeriodicEvents(world);
    }

    std::vector<ClockEventType> events_to_end;

    for (auto& [type, event] : event_manager_.getActiveEvents()) {
        updateEvent(world, type, event, deltaTime, digitPositions);

        event.remaining_time -= deltaTime;
        if (event.remaining_time <= 0.0) {
            events_to_end.push_back(type);
        }
    }

    for (auto type : events_to_end) {
        ActiveEvent* event = event_manager_.getActiveEvent(type);
        if (event) {
            endEvent(world, type, *event);
            event_manager_.removeActiveEvent(type);
        }
    }

    processQueuedEvents(world);
    updateDigitMaterialOverride();
}

bool ClockScenario::isEventBlockedByConflict(ClockEventType type) const
{
    if (type == ClockEventType::MELTDOWN) {
        return event_manager_.isEventActive(ClockEventType::MARQUEE);
    }

    if (type == ClockEventType::MARQUEE) {
        return event_manager_.isEventActive(ClockEventType::MELTDOWN);
    }

    return false;
}

void ClockScenario::queueEvent(ClockEventType type)
{
    if (!isEventAllowed(type)) {
        return;
    }

    auto already_queued = std::find(queued_events_.begin(), queued_events_.end(), type);
    if (already_queued != queued_events_.end()) {
        return;
    }

    queued_events_.push_back(type);
    spdlog::info(
        "ClockScenario: Queued {} event (waiting for conflict to end)", eventTypeName(type));
}

void ClockScenario::processQueuedEvents(World& world)
{
    if (queued_events_.empty()) {
        return;
    }

    std::vector<ClockEventType> remaining;
    remaining.reserve(queued_events_.size());

    for (ClockEventType type : queued_events_) {
        if (!isEventAllowed(type)) {
            continue;
        }

        if (event_manager_.isEventActive(type)) {
            continue;
        }

        if (isEventBlockedByConflict(type)) {
            remaining.push_back(type);
            continue;
        }

        startEvent(world, type);
    }

    queued_events_ = std::move(remaining);
}

bool ClockScenario::isEventAllowed(ClockEventType type) const
{
    switch (type) {
        case ClockEventType::COLOR_CYCLE:
            return config_.colorCycleEnabled;
        case ClockEventType::COLOR_SHOWCASE:
            return config_.colorShowcaseEnabled;
        case ClockEventType::DIGIT_SLIDE:
            return config_.digitSlideEnabled;
        case ClockEventType::DUCK:
            return config_.duckEnabled;
        case ClockEventType::MARQUEE:
            return config_.marqueeEnabled;
        case ClockEventType::MELTDOWN:
            return config_.meltdownEnabled;
        case ClockEventType::RAIN:
            return config_.rainEnabled;
    }
    return false;
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

        if (!isEventAllowed(type)) {
            continue;
        }

        if (event_manager_.isEventActive(type)) {
            continue;
        }

        if (event_manager_.isOnCooldown(type)) {
            continue;
        }

        double effective_chance = timing.chance * config_.eventFrequency;
        if (uniform_dist_(rng_) < effective_chance) {
            if (isEventBlockedByConflict(type)) {
                queueEvent(type);
            }
            else {
                startEvent(world, type);
            }
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

        if (!isEventAllowed(type)) {
            continue;
        }

        if (event_manager_.isEventActive(type)) {
            continue;
        }

        if (event_manager_.isOnCooldown(type)) {
            continue;
        }

        double effective_chance = timing.chance * config_.eventFrequency;
        if (effective_chance >= 1.0 || uniform_dist_(rng_) < effective_chance) {
            if (isEventBlockedByConflict(type)) {
                queueEvent(type);
            }
            else {
                startEvent(world, type);
            }
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
        ClockEvents::startColorCycle(state, config_.colorsPerSecond);
        event.state = state;
        spdlog::debug(
            "ClockScenario: Starting COLOR_CYCLE event (duration: {}s, rate: {} colors/sec)",
            eventTiming.duration,
            config_.colorsPerSecond);
    }
    else if (type == ClockEventType::MELTDOWN) {
        MeltdownEventState melt_state;
        ClockEvents::startMeltdown(melt_state, world);
        event.state = melt_state;
        spdlog::debug(
            "ClockScenario: Starting MELTDOWN event (duration: {}s)", eventTiming.duration);
    }
    else if (type == ClockEventType::RAIN) {
        event.state = RainEventState{};
        spdlog::debug("ClockScenario: Starting RAIN event (duration: {}s)", eventTiming.duration);
    }
    else if (type == ClockEventType::COLOR_SHOWCASE) {
        ColorShowcaseEventState state;
        const auto& showcase_materials = event_configs_.color_showcase.showcase_materials;
        Material::EnumType starting_material =
            ClockEvents::startColorShowcase(state, showcase_materials, rng_);
        Material::EnumType display_material = getColorShowcaseMaterial(state);
        event.state = state;
        if (showcase_materials.empty()) {
            spdlog::debug(
                "ClockScenario: Starting COLOR_SHOWCASE event (duration: {}s, showcase list empty; "
                "digits use {})",
                eventTiming.duration,
                toString(display_material));
        }
        else {
            spdlog::debug(
                "ClockScenario: Starting COLOR_SHOWCASE event (duration: {}s, starting color: {} "
                "at index {})",
                eventTiming.duration,
                toString(starting_material),
                state.current_index);
        }
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
        spdlog::debug(
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
        spdlog::debug(
            "ClockScenario: Starting MARQUEE event (duration: {}s, speed: {})",
            eventTiming.duration,
            event_configs_.marquee.scroll_speed);
    }
    else if (type == ClockEventType::DUCK) {
        DuckEventState duck_state;

        // Choose random entrance side. Door is 1 cell above floor.
        DoorSide entrance_side = (uniform_dist_(rng_) < 0.5) ? DoorSide::LEFT : DoorSide::RIGHT;
        constexpr double kDuckDoorOpenDelaySeconds = 2.0;
        constexpr uint32_t kCellsAboveFloor = 1;

        // Create entrance door (DoorManager computes position from side + cells_above_floor).
        DoorId entrance_door_id = door_manager_.createDoor(entrance_side, kCellsAboveFloor);
        ClockEvents::initializeDoorEntrySpawn(
            duck_state.entrance_spawn, entrance_door_id, entrance_side, kDuckDoorOpenDelaySeconds);

        // Create exit door on opposite side at same height.
        DoorSide exit_side =
            (duck_state.entrance_spawn.side == DoorSide::LEFT) ? DoorSide::RIGHT : DoorSide::LEFT;
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
        Vector2f entrance_light_pos =
            door_manager_.getLightPosition(duck_state.entrance_spawn.door_id, data);
        duck_state.entrance_light = lights.createLight(
            PointLight{ .position = entrance_light_pos,
                        .color = ColorNames::torchOrange(),
                        .intensity = kDoorLightOpenIntensity,
                        .radius = kDoorLightRadius,
                        .attenuation = kDoorLightAttenuation });

        // Exit door light (starts dim since door is closed).
        Vector2f exit_light_pos = door_manager_.getLightPosition(duck_state.exit_door_id, data);
        duck_state.exit_light = lights.createLight(
            PointLight{ .position = exit_light_pos,
                        .color = ColorNames::torchOrange(),
                        .intensity = kDoorLightClosedIntensity,
                        .radius = kDoorLightRadius,
                        .attenuation = kDoorLightAttenuation });

        // Open entrance door via DoorManager. Duck spawns after delay.
        door_manager_.openDoor(duck_state.entrance_spawn.door_id, world);
        duck_state.phase = DuckEventPhase::DOOR_OPENING;

        spdlog::info(
            "ClockScenario: Opening {} door for duck entrance",
            duck_state.entrance_spawn.side == DoorSide::LEFT ? "LEFT" : "RIGHT");

        event.state = std::move(duck_state);
        spdlog::info("ClockScenario: Starting DUCK event (duration: {}s)", eventTiming.duration);
    }

    event_manager_.addActiveEvent(type, std::move(event));
}

void ClockScenario::updateEvent(
    World& world,
    ClockEventType /*type*/,
    ActiveEvent& event,
    double deltaTime,
    std::vector<Vector2i>& digitPositions)
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
                updateDigitSlideEvent(world, state, deltaTime, digitPositions);
            }
            else if constexpr (std::is_same_v<T, DuckEventState>) {
                updateDuckEvent(world, state, event.remaining_time, deltaTime);
            }
            else if constexpr (std::is_same_v<T, MarqueeEventState>) {
                updateMarqueeEvent(world, state, event.remaining_time, deltaTime, digitPositions);
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
    ClockEvents::updateColorCycle(state, deltaTime);
}

void ClockScenario::updateColorShowcaseEvent(
    World& /*world*/, ColorShowcaseEventState& state, double /*deltaTime*/)
{
    const auto& showcase_materials = event_configs_.color_showcase.showcase_materials;
    ClockEvents::updateColorShowcase(
        state, showcase_materials, event_manager_.hasTimeChangedThisFrame());
}

void ClockScenario::updateDigitSlideEvent(
    World& world,
    DigitSlideEventState& state,
    double deltaTime,
    std::vector<Vector2i>& digitPositions)
{
    if (isEventActive(ClockEventType::MARQUEE)) {
        return;
    }

    std::string current_time = getCurrentTimeString();

    checkAndStartSlide(state.slide_state, last_drawn_time_, current_time);

    if (state.slide_state.active) {
        const CharacterMetrics& metrics = getMetrics();
        auto getWidth = metrics.widthFunction();
        MarqueeFrame frame = updateVerticalSlide(state.slide_state, deltaTime, getWidth);

        clearDigits(world);

        int dh = metrics.digitHeight;

        int content_width = calculateStringWidth(current_time, getWidth);
        int start_x = (world.getData().width - content_width) / 2;
        int start_y = (world.getData().height - dh) / 2;

        for (const auto& placement : frame.placements) {
            int x = start_x + static_cast<int>(placement.x);
            int y = start_y + static_cast<int>(placement.y);

            if (y + dh < 0 || y >= world.getData().height) {
                continue;
            }
            int charWidth = getWidth(placement.text);
            if (x + charWidth < 0 || x >= world.getData().width) {
                continue;
            }

            drawCharacter(world, placement.text, x, y, digitPositions);
        }
    }

    if (!state.slide_state.active) {
        state.slide_state.new_time_str = current_time;
    }

    last_drawn_time_ = current_time;
}

void ClockScenario::updateRainEvent(World& world, RainEventState& /*state*/, double deltaTime)
{
    ClockEvents::updateRain(world, deltaTime, rng_, uniform_dist_);
}

void ClockScenario::updateFloorObstacles(World& world, double deltaTime)
{
    if (!config_.obstacleCourseEnabled) {
        obstacleSpawnTimer_ = 0.0;
        if (!obstacle_manager_.getObstacles().empty()) {
            obstacle_manager_.clearAll(world);
        }
        return;
    }

    if (drain_manager_.isOpen()) {
        obstacleSpawnTimer_ = 0.0;
        if (!obstacle_manager_.getObstacles().empty()) {
            obstacle_manager_.clearAll(world);
        }
        return;
    }

    constexpr double spawnIntervalSeconds = 3.0;
    obstacleSpawnTimer_ += deltaTime;
    if (obstacleSpawnTimer_ < spawnIntervalSeconds) {
        return;
    }

    obstacleSpawnTimer_ = 0.0;
    obstacle_manager_.spawnObstacle(world, rng_, uniform_dist_);
}

void ClockScenario::updateMarqueeEvent(
    World& world,
    MarqueeEventState& state,
    double& remaining_time,
    double deltaTime,
    std::vector<Vector2i>& digitPositions)
{
    std::string time_str = getCurrentTimeString();
    const CharacterMetrics& metrics = getMetrics();
    auto getWidth = metrics.widthFunction();
    MarqueeFrame frame = updateHorizontalScroll(state.scroll_state, time_str, deltaTime, getWidth);

    MarqueeFrame combined_frame = frame;
    bool use_slide = false;
    ActiveEvent* slide_event = event_manager_.getActiveEvent(ClockEventType::DIGIT_SLIDE);
    if (slide_event) {
        auto& slide_state = std::get<DigitSlideEventState>(slide_event->state);
        checkAndStartSlide(slide_state.slide_state, last_drawn_time_, time_str);
        if (!slide_state.slide_state.active) {
            slide_state.slide_state.new_time_str = time_str;
        }
        combined_frame = updateVerticalSlide(slide_state.slide_state, deltaTime, getWidth);
        use_slide = true;
    }

    clearDigits(world);

    int dh = metrics.digitHeight;

    int content_width = static_cast<int>(state.scroll_state.content_width);
    int start_x = (world.getData().width - content_width) / 2;
    int start_y = (world.getData().height - dh) / 2;

    const auto& placements = use_slide ? combined_frame.placements : frame.placements;
    for (const auto& placement : placements) {
        double screen_x = start_x + placement.x - frame.viewportX;
        int charWidth = getWidth(placement.text);

        if (screen_x + charWidth < 0 || screen_x >= static_cast<double>(world.getData().width)) {
            continue;
        }

        int x = static_cast<int>(screen_x);
        int y = start_y + static_cast<int>(placement.y);
        if (y + dh < 0 || y >= world.getData().height) {
            continue;
        }

        drawCharacter(world, placement.text, x, y, digitPositions);
    }

    if (frame.finished) {
        remaining_time = 0.0;
    }

    last_drawn_time_ = time_str;
}

bool ClockScenario::spawnDuck(World& world, DuckEventState& state)
{
    // Spawn duck in the door opening.
    Vector2i spawn_pos =
        ClockEvents::getDoorEntryPosition(state.entrance_spawn, door_manager_, world.getData());

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
            return false;
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

    std::unique_ptr<DuckBrain> brain = std::make_unique<DuckBrain2>();

    state.organism_id = world.getOrganismManager().createDuck(
        world,
        static_cast<uint32_t>(spawn_pos.x),
        static_cast<uint32_t>(spawn_pos.y),
        std::move(brain));

    spdlog::info(
        "ClockScenario: Duck organism {} enters through {} door at ({}, {})",
        state.organism_id,
        state.entrance_spawn.side == DoorSide::LEFT ? "LEFT" : "RIGHT",
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

        auto handheld = std::make_unique<LightHandHeld>(std::move(flashlight));
        duck->setHandheldLight(std::move(handheld));
    }

    return true;
}

void ClockScenario::updateDuckEvent(
    World& world, DuckEventState& state, double& remaining_time, double deltaTime)
{
    if (state.phase == DuckEventPhase::DOOR_OPENING) {
        const ClockEvents::DoorEntrySpawnStep step =
            ClockEvents::updateDoorEntrySpawn(state.entrance_spawn, deltaTime);
        if (step == ClockEvents::DoorEntrySpawnStep::WaitingForDelay) {
            return;
        }

        if (step == ClockEvents::DoorEntrySpawnStep::ReadyToSpawn && spawnDuck(world, state)) {
            ClockEvents::markDoorEntrySpawnComplete(state.entrance_spawn);
            state.phase = DuckEventPhase::DUCK_ACTIVE;
        }
        else if (step == ClockEvents::DoorEntrySpawnStep::SpawnComplete) {
            state.phase = DuckEventPhase::DUCK_ACTIVE;
        }
        return;
    }

    // Phase 3: Duck exited, wait briefly then close door and end event.
    constexpr double DOOR_CLOSE_DELAY = 2.0;

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
    LightManager& lights = world.getLightManager();
    Vector2d duck_com{ 0.0, 0.0 };
    if (data.inBounds(duck_cell.x, duck_cell.y)) {
        duck_com = data.at(duck_cell.x, duck_cell.y).com;
    }

    // Door light intensity constants.
    constexpr float kDoorLightOpenIntensity = 1.0f;
    constexpr float kDoorLightClosedIntensity = 0.08f;

    // Close entrance door once duck moves away from it and schedule removal.
    if (ClockEvents::closeDoorAfterActorLeaves(
            state.entrance_spawn, door_manager_, world, duck_cell, std::chrono::seconds(2))) {
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
            (state.entrance_spawn.side == DoorSide::LEFT) ? (duck_com.x > 0.0) : (duck_com.x < 0.0);
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

void ClockScenario::endEvent(
    World& world, ClockEventType type, ActiveEvent& event, bool setCooldown)
{
    spdlog::debug("ClockScenario: Ending {} event", eventTypeName(type));

    if (type == ClockEventType::MELTDOWN) {
        // Convert any stray digit material (fallen digits) to water.
        auto& melt_state = std::get<MeltdownEventState>(event.state);
        convertStrayDigitMaterialToWater(world, melt_state.digit_material);
    }
    else if (type == ClockEventType::DUCK) {
        auto& state = std::get<DuckEventState>(event.state);

        if (state.organism_id != INVALID_ORGANISM_ID) {
            world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
        }

        // Remove door lights (RAII handles auto-cleanup).
        state.entrance_light.reset();
        state.exit_light.reset();

        // Close doors and schedule removal after a delay.
        door_manager_.closeDoor(state.entrance_spawn.door_id, world);
        door_manager_.closeDoor(state.exit_door_id, world);
        door_manager_.scheduleRemoval(state.entrance_spawn.door_id, std::chrono::seconds(2));
        door_manager_.scheduleRemoval(state.exit_door_id, std::chrono::seconds(2));
    }
    if (setCooldown) {
        const auto& timing = getEventTiming(type);
        event_manager_.setCooldown(type, timing.cooldown);
        spdlog::debug(
            "ClockScenario: Event {} on cooldown for {:.1f}s",
            eventTypeName(type),
            timing.cooldown);
    }
}

void ClockScenario::cancelAllEvents(World& world)
{
    spdlog::info("ClockScenario: Canceling all events");
    obstacleSpawnTimer_ = 0.0;
    digit_material_override_.reset();
    queued_events_.clear();

    for (auto& [type, event] : event_manager_.getActiveEvents()) {
        if (type == ClockEventType::DUCK) {
            auto& state = std::get<DuckEventState>(event.state);
            if (state.organism_id != INVALID_ORGANISM_ID) {
                world.getOrganismManager().removeOrganismFromWorld(world, state.organism_id);
            }

            state.entrance_light.reset();
            state.exit_light.reset();
        }
        else if (type == ClockEventType::MELTDOWN) {
            auto& melt_state = std::get<MeltdownEventState>(event.state);
            convertStrayDigitMaterialToWater(world, melt_state.digit_material);
        }
    }

    world.getOrganismManager().clear();

    event_manager_.clear();
    door_manager_.closeAllDoors(world);
    door_manager_.clear();
    obstacle_manager_.clearAll(world);
    drain_manager_.reset();
    storm_manager_.reset();
    redrawWalls(world);
}

bool ClockScenario::isMeltdownActive() const
{
    return event_manager_.isEventActive(ClockEventType::MELTDOWN);
}

void ClockScenario::updateMeltdownEvent(
    World& world, MeltdownEventState& state, double& remaining_time, double /*deltaTime*/)
{
    double event_duration = getEventTiming(ClockEventType::MELTDOWN).duration;
    ClockEvents::updateMeltdown(
        state,
        world,
        remaining_time,
        event_duration,
        drain_manager_.isOpen(),
        drain_manager_.getStartX(),
        drain_manager_.getEndX());

    // Make falling digit cells emissive so they glow while melting.
    const WorldData& data = world.getData();
    uint32_t color = getMaterialColor(getActiveDigitMaterial());
    float intensity = config_.glowConfig.digitIntensity;

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
    std::vector<Vector2i> tempDigitPositions;
    drawTime(world, tempDigitPositions);
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

double ClockScenario::countWaterInTopThird(const World& world) const
{
    const WorldData& data = world.getData();

    // Count water in the top 1/3 of the world.
    int top_third_end = data.height / 3;
    double total_water = 0.0;

    for (int y = 1; y < top_third_end; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.material_type == Material::EnumType::Water) {
                total_water += cell.fill_ratio;
            }
        }
    }

    return total_water;
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
        bool is_drain_cell = drain_manager_.isOpen() && x >= drain_manager_.getStartX()
            && x <= drain_manager_.getEndX();
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
        walls.push_back(
            { static_cast<int16_t>(roof_pos.x),
              static_cast<int16_t>(roof_pos.y),
              Material::EnumType::Wall });
    }

    // Door frame cells (wall above door, floor at door - render as wall/gray).
    for (const auto& frame_pos : door_manager_.getFramePositions(data)) {
        walls.push_back(
            { static_cast<int16_t>(frame_pos.x),
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
        bool is_drain_cell = drain_manager_.isOpen() && x >= drain_manager_.getStartX()
            && x <= drain_manager_.getEndX();

        if (is_pit_cell && !is_drain_cell) {
            Cell& cell = world.getData().at(x, height - 1);
            if (cell.material_type == Material::EnumType::Wall) {
                cell = Cell();
            }
        }
    }
}

} // namespace DirtSim
