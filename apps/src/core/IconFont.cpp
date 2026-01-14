#include "IconFont.h"

#include "Assert.h"
#include <lvgl.h>
#include <spdlog/spdlog.h>

#if LV_USE_FREETYPE
#include "lvgl/src/libs/freetype/lv_freetype.h"
#endif

namespace DirtSim {

namespace {

void ensureLvglInitialized()
{
    // Note: lv_freetype_init() is called automatically by lv_init() in LVGL.
    // We just need to ensure LVGL itself is initialized.
    if (!lv_is_initialized()) {
        lv_init();
    }
}

} // namespace

std::string IconFont::findFontPath()
{
    // Search paths in order of preference.
    const char* paths[] = { // Development paths (relative to build directory).
                            "../assets/fonts/fa-solid-900.ttf",
                            "assets/fonts/fa-solid-900.ttf",
                            "../src/../assets/fonts/fa-solid-900.ttf",

                            // Absolute development path.
                            "/home/data/workspace/dirtsim/apps/assets/fonts/fa-solid-900.ttf",

                            // Pi deployment path.
                            "/usr/share/fonts/fontawesome/fa-solid-900.ttf",

                            nullptr
    };

    for (const char** p = paths; *p != nullptr; ++p) {
        FILE* f = fopen(*p, "r");
        if (f) {
            fclose(f);
            return *p;
        }
    }

    return "";
}

IconFont::IconFont(int size)
{
#if LV_USE_FREETYPE
    ensureLvglInitialized();

    std::string path = findFontPath();
    DIRTSIM_ASSERT(!path.empty(), "FontAwesome TTF not found! Check assets/fonts/fa-solid-900.ttf");

    font_ = lv_freetype_font_create(
        path.c_str(),
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
        static_cast<uint32_t>(size),
        LV_FREETYPE_FONT_STYLE_NORMAL);

    DIRTSIM_ASSERT(font_ != nullptr, "Failed to load FontAwesome - check path exists");

    spdlog::info("IconFont: Loaded FontAwesome ({}px) from {}", size, path);
#else
    DIRTSIM_ASSERT(false, "IconFont requires LV_USE_FREETYPE=1");
    (void)size;
#endif
}

IconFont::~IconFont()
{
#if LV_USE_FREETYPE
    if (font_) {
        lv_freetype_font_delete(font_);
        font_ = nullptr;
    }
#endif
}

IconFont::IconFont(IconFont&& other) noexcept : font_(other.font_)
{
    other.font_ = nullptr;
}

IconFont& IconFont::operator=(IconFont&& other) noexcept
{
    if (this != &other) {
#if LV_USE_FREETYPE
        if (font_) {
            lv_freetype_font_delete(font_);
        }
#endif
        font_ = other.font_;
        other.font_ = nullptr;
    }
    return *this;
}

} // namespace DirtSim
