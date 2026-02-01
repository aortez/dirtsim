#pragma once

#include <string>

// Forward declaration to avoid LVGL in header.
struct _lv_font_t;
typedef struct _lv_font_t lv_font_t;

namespace DirtSim {

/**
 * @brief Loads FontAwesome icons at runtime via FreeType.
 *
 * Provides access to the full FontAwesome icon set rather than the limited
 * built-in LVGL symbols. The font is loaded from disk at construction time.
 * Can be used by UI for icons or by FontSampler to scan icons into the world.
 *
 * Usage:
 *   IconFont icons(48);  // Load at 48px size.
 *   lv_obj_set_style_text_font(label, icons.font(), 0);
 *   lv_label_set_text(label, IconFont::TREE);
 */
class IconFont {
public:
    /**
     * @brief Load FontAwesome at the specified size.
     * @param size Font size in pixels.
     *
     * Crashes if the font file cannot be loaded. This is intentional - a missing
     * font file indicates a deployment problem that should be fixed immediately.
     */
    explicit IconFont(int size);
    ~IconFont();

    // Non-copyable.
    IconFont(const IconFont&) = delete;
    IconFont& operator=(const IconFont&) = delete;

    // Movable.
    IconFont(IconFont&& other) noexcept;
    IconFont& operator=(IconFont&& other) noexcept;

    /**
     * @brief Get the loaded LVGL font.
     */
    lv_font_t* font() const { return font_; }

    /**
     * @brief Check if font was loaded successfully.
     */
    bool isValid() const { return font_ != nullptr; }

    // FontAwesome 6 icon codepoints (UTF-8 encoded).
    // See: https://fontawesome.com/search?o=r&m=free&s=solid

    // Navigation & UI.
    static constexpr const char* HOME = "\xEF\x80\x95";        // U+F015
    static constexpr const char* COG = "\xEF\x80\x93";         // U+F013 (settings)
    static constexpr const char* PLAY = "\xEF\x81\x8B";        // U+F04B
    static constexpr const char* PAUSE = "\xEF\x81\x8C";       // U+F04C
    static constexpr const char* STOP = "\xEF\x81\x8D";        // U+F04D
    static constexpr const char* ARROW_LEFT = "\xEF\x81\x93";  // U+F053
    static constexpr const char* ARROW_RIGHT = "\xEF\x81\x94"; // U+F054
    static constexpr const char* BARS = "\xEF\x83\x89";        // U+F0C9 (hamburger menu)
    static constexpr const char* XMARK = "\xEF\x80\x8D";       // U+F00D (close)
    static constexpr const char* CHECK = "\xEF\x80\x8C";       // U+F00C

    // Nature & Biology.
    static constexpr const char* SEEDLING = "\xEF\x93\x98"; // U+F4D8
    static constexpr const char* TREE = "\xEF\x86\xBB";     // U+F1BB
    static constexpr const char* LEAF = "\xEF\x81\xAC";     // U+F06C
    static constexpr const char* DROPLET = "\xEF\x81\x83";  // U+F043 (water/tint)
    static constexpr const char* SUN = "\xEF\x86\x85";      // U+F185
    static constexpr const char* MOON = "\xEF\x86\x86";     // U+F186
    static constexpr const char* CLOUD = "\xEF\x83\x82";    // U+F0C2
    static constexpr const char* MOUNTAIN = "\xEF\x9B\xBC"; // U+F6FC
    static constexpr const char* WATER = "\xEF\x9D\xB3";    // U+F773

    // Science & Evolution.
    static constexpr const char* CHART_LINE = "\xEF\x88\x81"; // U+F201
    static constexpr const char* DNA = "\xEF\x91\xB1";        // U+F471
    static constexpr const char* BRAIN = "\xEF\x97\x9C";      // U+F5DC
    static constexpr const char* FLASK = "\xEF\x83\x83";      // U+F0C3
    static constexpr const char* MICROSCOPE = "\xEF\x98\x90"; // U+F610
    static constexpr const char* ATOM = "\xEF\x97\x92";       // U+F5D2
    static constexpr const char* VIRUS = "\xEE\x81\x99";      // U+E059

    // Simulation & Physics.
    static constexpr const char* BOLT = "\xEF\x83\xA7";      // U+F0E7 (lightning/energy)
    static constexpr const char* FIRE = "\xEF\x81\xAD";      // U+F06D
    static constexpr const char* SNOWFLAKE = "\xEF\x8B\x9C"; // U+F2DC
    static constexpr const char* WIND = "\xEF\x9C\xAE";      // U+F72E
    static constexpr const char* CUBE = "\xEF\x86\xB2";      // U+F1B2
    static constexpr const char* CUBES = "\xEF\x86\xB3";     // U+F1B3
    static constexpr const char* GLOBE = "\xEF\x82\xAC";     // U+F0AC
    static constexpr const char* WIFI = "\xEF\x87\xAB";      // U+F1EB

    // Status & Feedback.
    static constexpr const char* SKULL = "\xEF\x95\x8C";                // U+F54C
    static constexpr const char* HEART = "\xEF\x80\x84";                // U+F004
    static constexpr const char* STAR = "\xEF\x80\x85";                 // U+F005
    static constexpr const char* EYE = "\xEF\x81\xAE";                  // U+F06E
    static constexpr const char* EYE_SLASH = "\xEF\x81\xB0";            // U+F070
    static constexpr const char* CIRCLE_INFO = "\xEF\x81\x9A";          // U+F05A
    static constexpr const char* TRIANGLE_EXCLAMATION = "\xEF\x81\xB1"; // U+F071

    // Actions.
    static constexpr const char* ROTATE = "\xEF\x80\xA1";   // U+F021 (refresh)
    static constexpr const char* SHUFFLE = "\xEF\x81\xB4";  // U+F074
    static constexpr const char* PLUS = "\xEF\x81\xA7";     // U+F067
    static constexpr const char* MINUS = "\xEF\x81\xA8";    // U+F068
    static constexpr const char* TRASH = "\xEF\x8B\xAD";    // U+F2ED
    static constexpr const char* DOWNLOAD = "\xEF\x80\x99"; // U+F019
    static constexpr const char* UPLOAD = "\xEF\x82\x93";   // U+F093

    // Files & Storage.
    static constexpr const char* FILE_CABINET = "\xEF\x86\x87"; // U+F187 (box-archive).

    // Media.
    static constexpr const char* FILM = "\xEF\x80\x88";        // U+F008 (video/scenario)
    static constexpr const char* CAMERA = "\xEF\x80\xB0";      // U+F030
    static constexpr const char* IMAGE = "\xEF\x80\xBE";       // U+F03E
    static constexpr const char* WAVE_SQUARE = "\xEF\xA0\xBE"; // U+F83E

private:
    lv_font_t* font_ = nullptr;

    static std::string findFontPath();
};

} // namespace DirtSim
