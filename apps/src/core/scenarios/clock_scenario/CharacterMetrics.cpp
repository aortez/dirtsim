#include "CharacterMetrics.h"

#include "core/scenarios/ClockFontPatterns.h"
#include <array>

namespace DirtSim {

CharacterMetrics::CharacterMetrics(Config::ClockFont f) : font(f)
{
    switch (font) {
        case Config::ClockFont::DotMatrix:
            colonPadding = ClockFonts::DOT_MATRIX_COLON_PADDING;
            colonWidth = ClockFonts::DOT_MATRIX_COLON_WIDTH;
            digitHeight = ClockFonts::DOT_MATRIX_HEIGHT;
            digitWidth = ClockFonts::DOT_MATRIX_WIDTH;
            gap = ClockFonts::DOT_MATRIX_GAP;
            break;

        case Config::ClockFont::Montserrat24:
            colonPadding = ClockFonts::MONTSERRAT24_COLON_PADDING;
            colonWidth = ClockFonts::MONTSERRAT24_COLON_WIDTH;
            digitHeight = ClockFonts::MONTSERRAT24_HEIGHT;
            digitWidth = ClockFonts::MONTSERRAT24_WIDTH;
            gap = ClockFonts::MONTSERRAT24_GAP;
            break;

        case Config::ClockFont::NotoColorEmoji:
            colonPadding = ClockFonts::NOTO_EMOJI_COLON_PADDING;
            colonWidth = ClockFonts::NOTO_EMOJI_COLON_WIDTH;
            digitHeight = ClockFonts::NOTO_EMOJI_HEIGHT;
            digitWidth = ClockFonts::NOTO_EMOJI_WIDTH;
            gap = ClockFonts::NOTO_EMOJI_GAP;
            break;

        case Config::ClockFont::Segment7:
            colonPadding = ClockFonts::SEGMENT7_COLON_PADDING;
            colonWidth = ClockFonts::SEGMENT7_COLON_WIDTH;
            digitHeight = ClockFonts::SEGMENT7_HEIGHT;
            digitWidth = ClockFonts::SEGMENT7_WIDTH;
            gap = ClockFonts::SEGMENT7_GAP;
            break;

        case Config::ClockFont::Segment7ExtraTall:
            colonPadding = ClockFonts::SEGMENT7_EXTRA_TALL_COLON_PADDING;
            colonWidth = ClockFonts::SEGMENT7_EXTRA_TALL_COLON_WIDTH;
            digitHeight = ClockFonts::SEGMENT7_EXTRA_TALL_HEIGHT;
            digitWidth = ClockFonts::SEGMENT7_EXTRA_TALL_WIDTH;
            gap = ClockFonts::SEGMENT7_EXTRA_TALL_GAP;
            break;

        case Config::ClockFont::Segment7Jumbo:
            colonPadding = ClockFonts::SEGMENT7_JUMBO_COLON_PADDING;
            colonWidth = ClockFonts::SEGMENT7_JUMBO_COLON_WIDTH;
            digitHeight = ClockFonts::SEGMENT7_JUMBO_HEIGHT;
            digitWidth = ClockFonts::SEGMENT7_JUMBO_WIDTH;
            gap = ClockFonts::SEGMENT7_JUMBO_GAP;
            break;

        case Config::ClockFont::Segment7Large:
            colonPadding = ClockFonts::SEGMENT7_LARGE_COLON_PADDING;
            colonWidth = ClockFonts::SEGMENT7_LARGE_COLON_WIDTH;
            digitHeight = ClockFonts::SEGMENT7_LARGE_HEIGHT;
            digitWidth = ClockFonts::SEGMENT7_LARGE_WIDTH;
            gap = ClockFonts::SEGMENT7_LARGE_GAP;
            break;

        case Config::ClockFont::Segment7Tall:
            colonPadding = ClockFonts::SEGMENT7_TALL_COLON_PADDING;
            colonWidth = ClockFonts::SEGMENT7_TALL_COLON_WIDTH;
            digitHeight = ClockFonts::SEGMENT7_TALL_HEIGHT;
            digitWidth = ClockFonts::SEGMENT7_TALL_WIDTH;
            gap = ClockFonts::SEGMENT7_TALL_GAP;
            break;
    }
}

int CharacterMetrics::charWidth(const std::string& utf8Char) const
{
    if (utf8Char.empty()) {
        return 0;
    }
    if (utf8Char == ":") {
        return colonWidth;
    }
    if (utf8Char == " ") {
        return gap;
    }
    return digitWidth;
}

bool CharacterMetrics::isColorFont() const
{
    return font == Config::ClockFont::NotoColorEmoji;
}

bool CharacterMetrics::usesFontSampler() const
{
    return font == Config::ClockFont::Montserrat24 || font == Config::ClockFont::NotoColorEmoji;
}

std::function<int(const std::string&)> CharacterMetrics::widthFunction() const
{
    return [metrics = *this](const std::string& utf8Char) { return metrics.charWidth(utf8Char); };
}

const CharacterMetrics& getFont(Config::ClockFont font)
{
    static const std::array<CharacterMetrics, 8> metrics = {
        CharacterMetrics(Config::ClockFont::DotMatrix),
        CharacterMetrics(Config::ClockFont::Montserrat24),
        CharacterMetrics(Config::ClockFont::NotoColorEmoji),
        CharacterMetrics(Config::ClockFont::Segment7),
        CharacterMetrics(Config::ClockFont::Segment7ExtraTall),
        CharacterMetrics(Config::ClockFont::Segment7Jumbo),
        CharacterMetrics(Config::ClockFont::Segment7Large),
        CharacterMetrics(Config::ClockFont::Segment7Tall),
    };
    return metrics[static_cast<size_t>(font)];
}

} // namespace DirtSim
